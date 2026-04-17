(function () {
  function isSilentReplyText(text) {
    return typeof text === "string" && /^\s*NO_REPLY\s*$/i.test(text);
  }

  function parseTextFromMessage(message) {
    if (!message || typeof message !== "object") {
      return "";
    }

    if (typeof message.text === "string") {
      return message.text;
    }

    if (Array.isArray(message.content)) {
      const item = message.content.find(
        (x) => x && x.type === "text" && typeof x.text === "string");
      return item ? item.text : "";
    }

    return "";
  }

  function dataUrlToBase64(dataUrl) {
    const match = /^data:([^;]+);base64,(.+)$/i.exec(String(dataUrl || ""));
    if (!match) {
      return null;
    }

    return {
      mimeType: match[1],
      content: match[2],
    };
  }

  function createController(options) {
    const opts = options || {};
    const state = opts.state;
    if (!state) {
      throw new Error("chat-controller requires state");
    }

    const addMessage = opts.addMessage || function () {};
    const addOrReplaceStream = opts.addOrReplaceStream || function () {};
    const finalizeStream = opts.finalizeStream || function () {};
    const updateComposerState = opts.updateComposerState || function () {};

    function nextId() {
      return `web-${Date.now()}-${Math.floor(Math.random() * 10000)}`;
    }

    function post(message) {
      if (!window.chrome || !window.chrome.webview) {
        state.bridgeQueue.push(message);
        return;
      }

      window.chrome.webview.postMessage(message);
    }

    function flushQueue() {
      while (state.bridgeQueue.length > 0) {
        post(state.bridgeQueue.shift());
      }
    }

    function request(method, params) {
      return new Promise((resolve, reject) => {
        const id = nextId();
        state.pending.set(id, { resolve, reject });
        post({ channel: "blazeclaw.gateway.rpc", id, method, params });
      });
    }

    async function loadHistory() {
      try {
        state.streamText = "";
        state.runId = null;
        finalizeStream();

        const response = await request("chat.history", {
          sessionKey: state.sessionKey,
          limit: 200,
        });

        const messages = Array.isArray(response && response.payload && response.payload.messages)
          ? response.payload.messages
          : Array.isArray(response && response.messages)
            ? response.messages
            : [];

        for (const message of messages) {
          const role = typeof message.role === "string" ? message.role : "assistant";
          const text = parseTextFromMessage(message);
          if (!text || isSilentReplyText(text)) {
            continue;
          }

          addMessage(text, role === "user" ? "self" : "peer");
        }
      } catch (error) {
        addMessage(`history error: ${String(error)}`, "error");
      }
    }

    async function send(forceError) {
      const message = String(state.inputEl.value || "").trim();
      if ((!message && state.attachments.length === 0) || !state.bridgeAvailable) {
        return;
      }

      state.inputEl.value = "";
      if (message) {
        addMessage(message, "self");
      }

      if (state.attachments.length > 0) {
        addMessage(
          `[Attachment] ${state.attachments.map((item) => item.name).join(", ")}`,
          "self");
      }

      state.runId = nextId();
      state.streamText = "";
      updateComposerState();

      const apiAttachments = state.attachments
        .map((item) => {
          const parsed = dataUrlToBase64(item.dataUrl);
          if (!parsed) {
            return null;
          }

          return {
            type: "image",
            mimeType: parsed.mimeType || item.mimeType || "image/*",
            content: parsed.content,
          };
        })
        .filter((x) => x !== null);

      try {
        const sendResult = await request("chat.send", {
          sessionKey: state.sessionKey,
          message,
          deliver: false,
          idempotencyKey: state.runId,
          forceError: Boolean(forceError),
          attachments: apiAttachments,
        });

        const serverRunId =
          sendResult &&
          sendResult.payload &&
          typeof sendResult.payload.runId === "string"
            ? sendResult.payload.runId
            : "";

        if (serverRunId) {
          state.runId = serverRunId;
        }

        state.attachments = [];
      } catch (error) {
        addMessage(`send error: ${String(error)}`, "error");
        state.runId = null;
      }

      updateComposerState();
    }

    async function abort() {
      if (!state.runId || !state.bridgeAvailable) {
        return;
      }

      try {
        await request("chat.abort", {
          sessionKey: state.sessionKey,
          runId: state.runId,
        });
      } catch (error) {
        addMessage(`abort error: ${String(error)}`, "error");
      }

      updateComposerState();
    }

    function handleRpcResult(message) {
      const slot = state.pending.get(message.id);
      if (!slot) {
        return;
      }

      state.pending.delete(message.id);
      if (message.ok) {
        slot.resolve(message);
      } else {
        slot.reject(
          message.error ? message.error.message || "request failed" : "request failed");
      }
    }

    function consumeTerminalText(message) {
      const parsed = parseTextFromMessage(message);
      if (parsed && !isSilentReplyText(parsed)) {
        return parsed;
      }

      if (state.streamText && !isSilentReplyText(state.streamText)) {
        return state.streamText;
      }

      return "";
    }

    function applyDeltaText(text) {
      if (!text || isSilentReplyText(text)) {
        return;
      }

      if (text.length >= state.streamText.length) {
        state.streamText = text;
        addOrReplaceStream(state.streamText);
      }
    }

    function clearRunState() {
      state.streamText = "";
      state.runId = null;
      if (state.abortBtn) {
        state.abortBtn.disabled = true;
      }
    }

    function readFileAsDataUrl(file) {
      return new Promise((resolve, reject) => {
        const reader = new FileReader();
        reader.onload = () => resolve(String(reader.result || ""));
        reader.onerror = () => reject(new Error("file read failed"));
        reader.readAsDataURL(file);
      });
    }

    async function addAttachmentFiles(files) {
      const fileList = Array.from(files || []);
      if (fileList.length === 0) {
        return;
      }

      for (const file of fileList) {
        if (!file || !String(file.type || "").startsWith("image/")) {
          addMessage(`attachment skipped: ${file ? file.name : "unknown"}`, "error");
          continue;
        }

        try {
          const dataUrl = await readFileAsDataUrl(file);
          state.attachments.push({
            name: file.name,
            mimeType: file.type || "image/*",
            dataUrl,
          });
        } catch (_) {
          addMessage(`attachment read error: ${file.name}`, "error");
        }
      }

      if (state.attachInput) {
        state.attachInput.value = "";
      }

      updateComposerState();
    }

    return {
      nextId,
      post,
      flushQueue,
      request,
      loadHistory,
      send,
      abort,
      handleRpcResult,
      consumeTerminalText,
      applyDeltaText,
      clearRunState,
      parseTextFromMessage,
      isSilentReplyText,
      addAttachmentFiles,
    };
  }

  window.BlazeClawChatController = {
    createController,
  };
})();
