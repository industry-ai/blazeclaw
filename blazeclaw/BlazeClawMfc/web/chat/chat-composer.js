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

        const updateComposerState = opts.updateComposerState || function () { };

        state.slashHints = Array.isArray(state.slashHints) ? state.slashHints : [];
        state.slashActiveIndex = Number.isInteger(state.slashActiveIndex)
            ? state.slashActiveIndex
            : 0;

        function renderSelect(selectEl, options, selectedValue, fallbackLabel) {
            if (!selectEl) {
                return;
            }

            const list = Array.isArray(options) ? options : [];
            selectEl.innerHTML = "";

            if (!list.length) {
                const optionEl = document.createElement("option");
                optionEl.value = "";
                optionEl.textContent = fallbackLabel;
                selectEl.appendChild(optionEl);
                return;
            }

            for (const item of list) {
                const optionEl = document.createElement("option");
                const id = typeof item === "string" ? item : String(item.id || "");
                const label = typeof item === "string"
                    ? item
                    : String(item.label || item.id || id);
                optionEl.value = id;
                optionEl.textContent = label;
                if (id === selectedValue) {
                    optionEl.selected = true;
                }
                selectEl.appendChild(optionEl);
            }
        }

        function hideSlashMenu() {
            state.slashHints = [];
            state.slashActiveIndex = 0;
            if (state.slashMenuEl) {
                state.slashMenuEl.style.display = "none";
                state.slashMenuEl.innerHTML = "";
            }
        }

        function applySlashHint(index) {
            if (!Array.isArray(state.slashHints) || !state.slashHints.length) {
                return;
            }

            const safeIndex = Math.max(0, Math.min(index, state.slashHints.length - 1));
            const selected = state.slashHints[safeIndex];
            if (!selected || !state.inputEl) {
                return;
            }

            state.inputEl.value = `/${selected.name} `;
            hideSlashMenu();
            updateComposerState();
            state.inputEl.focus();
        }

        function renderSlashMenu() {
            if (!state.slashMenuEl) {
                return;
            }

            if (!Array.isArray(state.slashHints) || !state.slashHints.length) {
                hideSlashMenu();
                return;
            }

            state.slashMenuEl.innerHTML = "";
            state.slashMenuEl.style.display = "block";

            state.slashHints.forEach((item, index) => {
                const row = document.createElement("div");
                row.className = "slash-item";
                if (index === state.slashActiveIndex) {
                    row.classList.add("active");
                }

                const name = document.createElement("div");
                name.className = "slash-name";
                name.textContent = `/${item.name}`;
                row.appendChild(name);

                const desc = document.createElement("div");
                desc.className = "slash-desc";
                desc.textContent = item.description || "slash command";
                row.appendChild(desc);

                row.addEventListener("mousedown", (event) => {
                    event.preventDefault();
                    applySlashHint(index);
                });

                state.slashMenuEl.appendChild(row);
            });
        }

        async function updateSlashMenuFromInput() {
            if (!state.inputEl) {
                return;
            }

            const text = String(state.inputEl.value || "");
            if (!text.startsWith("/")) {
                hideSlashMenu();
                return;
            }

            const query = text.slice(1).trim().split(/\s+/)[0] || "";
            state.slashHints = await controller.getSlashCommandHints(query);
            state.slashActiveIndex = 0;
            renderSlashMenu();
        }

        async function bootstrapSelectors() {
            const sessions = await controller.loadSessionOptions();
            renderSelect(state.sessionSelect, sessions, state.sessionKey, "session");

            const models = await controller.loadModelOptions();
            renderSelect(state.modelSelect, models, state.selectedModel, "model");

            const thinkingOptions = controller.loadThinkingOptions();
            renderSelect(
                state.thinkingSelect,
                thinkingOptions.map((item) => ({ id: item, label: `thinking:${item}` })),
                state.thinkingLevel,
                "thinking");

            controller.restoreDraftForSession();
            updateComposerState();
        }

        async function handleAttachChange() {
            await controller.addAttachmentFiles(state.attachInput ? state.attachInput.files : []);
            updateComposerState();
        }

        function bind() {
            if (state.sendBtn) {
                state.sendBtn.addEventListener("click", () => {
                    hideSlashMenu();
                    void controller.send(false);
                });
            }

            if (state.sendErrBtn) {
                state.sendErrBtn.addEventListener("click", () => {
                    hideSlashMenu();
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
                    hideSlashMenu();
                    void controller.abort();
                });
            }

            if (state.sessionSelect) {
                state.sessionSelect.addEventListener("change", () => {
                    const target = state.sessionSelect.value;
                    hideSlashMenu();
                    void controller.switchSession(target);
                });
            }

            if (state.modelSelect) {
                state.modelSelect.addEventListener("change", () => {
                    const target = state.modelSelect.value;
                    hideSlashMenu();
                    void controller.applyModelSelection(target);
                });
            }

            if (state.thinkingSelect) {
                state.thinkingSelect.addEventListener("change", () => {
                    const target = state.thinkingSelect.value;
                    controller.applyThinkingLevel(target);
                    hideSlashMenu();
                });
            }

            if (state.inputEl) {
                state.inputEl.addEventListener("input", () => {
                    controller.persistDraftForSession();
                    void updateSlashMenuFromInput();
                    updateComposerState();
                });

                state.inputEl.addEventListener("keydown", (event) => {
                    if (event.key === "ArrowUp" &&
                        state.inputEl.selectionStart === 0 &&
                        state.inputEl.selectionEnd === 0 &&
                        !event.shiftKey) {
                        event.preventDefault();
                        const recalled = controller.recallInputHistory(1);
                        state.inputEl.value = recalled;
                        controller.persistDraftForSession();
                        updateComposerState();
                        return;
                    }

                    if (event.key === "ArrowDown" &&
                        state.inputEl.selectionStart === 0 &&
                        state.inputEl.selectionEnd === 0 &&
                        !event.shiftKey) {
                        event.preventDefault();
                        const recalled = controller.recallInputHistory(-1);
                        state.inputEl.value = recalled;
                        controller.persistDraftForSession();
                        updateComposerState();
                        return;
                    }

                    if (state.slashHints.length > 0) {
                        if (event.key === "ArrowDown") {
                            event.preventDefault();
                            state.slashActiveIndex = (state.slashActiveIndex + 1) % state.slashHints.length;
                            renderSlashMenu();
                            return;
                        }

                        if (event.key === "ArrowUp") {
                            event.preventDefault();
                            state.slashActiveIndex =
                                (state.slashActiveIndex + state.slashHints.length - 1) % state.slashHints.length;
                            renderSlashMenu();
                            return;
                        }

                        if (event.key === "Tab") {
                            event.preventDefault();
                            applySlashHint(state.slashActiveIndex);
                            return;
                        }

                        if (event.key === "Escape") {
                            event.preventDefault();
                            hideSlashMenu();
                            return;
                        }
                    }

                    if (event.key === "Enter" && !event.shiftKey) {
                        event.preventDefault();
                        hideSlashMenu();
                        void controller.send(false);
                    }
                });
            }

            document.addEventListener("click", (event) => {
                if (!state.slashMenuEl || !state.inputEl) {
                    return;
                }

                const target = event.target;
                if (state.slashMenuEl.contains(target) || target === state.inputEl) {
                    return;
                }

                hideSlashMenu();
            });

            void bootstrapSelectors();
            void controller.getSlashCommandHints("");
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
