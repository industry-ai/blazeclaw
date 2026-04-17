(function () {
  function createComposerModule(options) {
    const opts = options || {};
    const state = opts.state;
    if (!state) {
      throw new Error("chat-composer requires state");
    }

    const controller = opts.controller;
    if (!controller) {
      throw new Error("chat-composer requires controller");
    }

    const updateComposerState = opts.updateComposerState || function () {};

    async function handleAttachChange() {
      await controller.addAttachmentFiles(state.attachInput ? state.attachInput.files : []);
      updateComposerState();
    }

    function bind() {
      if (state.sendBtn) {
        state.sendBtn.addEventListener("click", () => {
          void controller.send(false);
        });
      }

      if (state.sendErrBtn) {
        state.sendErrBtn.addEventListener("click", () => {
          void controller.send(true);
        });
      }

      if (state.attachBtn && state.attachInput) {
        state.attachBtn.addEventListener("click", () => state.attachInput.click());
      }

      if (state.attachInput) {
        state.attachInput.addEventListener("change", () => {
          void handleAttachChange();
        });
      }

      if (state.abortBtn) {
        state.abortBtn.addEventListener("click", () => {
          void controller.abort();
        });
      }

      if (state.inputEl) {
        state.inputEl.addEventListener("input", () => {
          updateComposerState();
        });

        state.inputEl.addEventListener("keydown", (event) => {
          if (event.key === "Enter" && !event.shiftKey) {
            event.preventDefault();
            void controller.send(false);
          }
        });
      }
    }

    return {
      bind,
      handleAttachChange,
    };
  }

  window.BlazeClawChatComposer = {
    createComposerModule,
  };
})();
