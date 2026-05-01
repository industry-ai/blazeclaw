(function () {
    const FORBIDDEN_KEYS = new Set(["__proto__", "prototype", "constructor"]);

    function cloneConfigObject(value) {
        if (typeof structuredClone === "function") {
            return structuredClone(value);
        }

        return JSON.parse(JSON.stringify(value));
    }

    function serializeConfigForm(form) {
        return `${JSON.stringify(form, null, 2).trimEnd()}\n`;
    }

    function isForbiddenKey(key) {
        if (typeof key !== "string") {
            return false;
        }

        return FORBIDDEN_KEYS.has(key);
    }

    function resolvePathContainer(obj, path, createMissing) {
        if (!Array.isArray(path) || path.length === 0 || path.some(isForbiddenKey)) {
            return null;
        }

        let current = obj;
        for (let i = 0; i < path.length - 1; i += 1) {
            const key = path[i];
            const nextKey = path[i + 1];

            if (typeof key === "number") {
                if (!Array.isArray(current)) {
                    return null;
                }

                if (current[key] == null) {
                    if (!createMissing) {
                        return null;
                    }

                    if (typeof nextKey === "number") {
                        current[key] = [];
                    } else {
                        current[key] = {};
                    }
                }

                current = current[key];
                continue;
            }

            if (typeof current !== "object" || current == null) {
                return null;
            }

            if (current[key] == null) {
                if (!createMissing) {
                    return null;
                }

                if (typeof nextKey === "number") {
                    current[key] = [];
                } else {
                    current[key] = {};
                }
            }

            current = current[key];
        }

        return {
            current,
            lastKey: path[path.length - 1],
        };
    }

    function setPathValue(obj, path, value) {
        const container = resolvePathContainer(obj, path, true);
        if (!container) {
            return;
        }

        if (typeof container.lastKey === "number") {
            if (Array.isArray(container.current)) {
                container.current[container.lastKey] = value;
            }
            return;
        }

        if (typeof container.current === "object" && container.current != null) {
            container.current[container.lastKey] = value;
        }
    }

    function removePathValue(obj, path) {
        const container = resolvePathContainer(obj, path, false);
        if (!container) {
            return;
        }

        if (typeof container.lastKey === "number") {
            if (Array.isArray(container.current)) {
                container.current.splice(container.lastKey, 1);
            }
            return;
        }

        if (typeof container.current === "object" && container.current != null) {
            delete container.current[container.lastKey];
        }
    }

    window.BlazeClawConfigFormUtils = {
        cloneConfigObject,
        serializeConfigForm,
        setPathValue,
        removePathValue,
    };
})();
