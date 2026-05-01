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

        let updateComposerState = function () { };
        if (typeof opts.updateComposerState === "function") {
            updateComposerState = opts.updateComposerState;
        }

        if (!Array.isArray(state.slashHints)) {
            state.slashHints = [];
        }
        if (!Number.isInteger(state.slashActiveIndex)) {
            state.slashActiveIndex = 0;
        }

        function normalizeSelectOptions(options) {
            if (Array.isArray(options)) {
                return options;
            }
            return [];
        }

        function getSelectOptionId(item) {
            if (typeof item === "string") {
                return item;
            }
            return String(item.id || "");
        }

        function getSelectOptionLabel(item, fallbackId) {
            if (typeof item === "string") {
                return item;
            }

            if (item.label || item.id) {
                return String(item.label || item.id || "");
            }

            return fallbackId;
        }

        function renderSelect(selectEl, options, selectedValue, fallbackLabel) {
            if (!selectEl) {
                return;
            }

            const list = normalizeSelectOptions(options);
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
                const id = getSelectOptionId(item);
                const label = getSelectOptionLabel(item, id);
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

            state.slashHints.forEach(function (item, index) {
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

                row.addEventListener("mousedown", function (event) {
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
            const thinkingSelectOptions = thinkingOptions.map(function (item) {
                return { id: item, label: `thinking:${item}` };
            });
            renderSelect(
                state.thinkingSelect,
                thinkingSelectOptions,
                state.thinkingLevel,
                "thinking");

            controller.restoreDraftForSession();
            updateComposerState();
        }

        async function handleAttachChange() {
            let files = [];
            if (state.attachInput) {
                files = state.attachInput.files;
            }

            await controller.addAttachmentFiles(files);
            updateComposerState();
        }

        function bind() {
            if (state.sendBtn) {
                state.sendBtn.addEventListener("click", function () {
                    hideSlashMenu();
                    void controller.send(false);
                });
            }

            if (state.sendErrBtn) {
                state.sendErrBtn.addEventListener("click", function () {
                    hideSlashMenu();
                    void controller.send(true);
                });
            }

            if (state.attachBtn && state.attachInput) {
                state.attachBtn.addEventListener("click", function () {
                    state.attachInput.click();
                });
            }

            if (state.attachInput) {
                state.attachInput.addEventListener("change", function () {
                    void handleAttachChange();
                });
            }

            if (state.abortBtn) {
                state.abortBtn.addEventListener("click", function () {
                    hideSlashMenu();
                    void controller.abort();
                });
            }

            if (state.sessionSelect) {
                state.sessionSelect.addEventListener("change", function () {
                    const target = state.sessionSelect.value;
                    hideSlashMenu();
                    void controller.switchSession(target).finally(function () {
                        if (typeof state.onSessionChanged === "function") {
                            state.onSessionChanged(state.sessionKey);
                        }
                    });
                });
            }

            if (state.modelSelect) {
                state.modelSelect.addEventListener("change", function () {
                    const target = state.modelSelect.value;
                    hideSlashMenu();
                    void controller.applyModelSelection(target).finally(function () {
                        if (typeof state.onModelChanged === "function") {
                            state.onModelChanged(state.selectedModel);
                        }
                    });
                });
            }

            if (state.thinkingSelect) {
                state.thinkingSelect.addEventListener("change", function () {
                    const target = state.thinkingSelect.value;
                    controller.applyThinkingLevel(target);
                    hideSlashMenu();
                });
            }

            if (state.inputEl) {
                state.inputEl.addEventListener("input", function () {
                    controller.persistDraftForSession();
                    void updateSlashMenuFromInput();
                    updateComposerState();
                });

                state.inputEl.addEventListener("keydown", function (event) {
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

            document.addEventListener("click", function (event) {
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
