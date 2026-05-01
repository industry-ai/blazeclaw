(function () {
    function schemaType(schema) {
        if (!schema || typeof schema !== "object") {
            return null;
        }

        const type = schema.type;
        if (typeof type === "string") {
            return type;
        }

        if (Array.isArray(type)) {
            const nonNull = type.find(function (entry) {
                return entry !== "null";
            });
            if (typeof nonNull === "string") {
                return nonNull;
            }
            return null;
        }

        return null;
    }

    function coerceNumberString(value, integer) {
        const trimmed = String(value || "").trim();
        if (trimmed === "") {
            return undefined;
        }

        const parsed = Number(trimmed);
        if (!Number.isFinite(parsed)) {
            return value;
        }

        if (integer && !Number.isInteger(parsed)) {
            return value;
        }

        return parsed;
    }

    function coerceBooleanString(value) {
        const trimmed = String(value || "").trim();
        if (trimmed === "true") {
            return true;
        }
        if (trimmed === "false") {
            return false;
        }

        return value;
    }

    function isNullableVariant(variant) {
        if (!variant || typeof variant !== "object") {
            return false;
        }

        if (variant.type === "null") {
            return true;
        }

        return Array.isArray(variant.type) && variant.type.indexOf("null") >= 0;
    }

    function coerceFormValues(value, schema) {
        if (value === null || value === undefined) {
            return value;
        }

        if (!schema || typeof schema !== "object") {
            return value;
        }

        if (Array.isArray(schema.allOf) && schema.allOf.length > 0) {
            let next = value;
            schema.allOf.forEach(function (segment) {
                next = coerceFormValues(next, segment);
            });
            return next;
        }

        const type = schemaType(schema);

        if (schema.anyOf || schema.oneOf) {
            const variants = (schema.anyOf || schema.oneOf || []).filter(function (variant) {
                return !isNullableVariant(variant);
            });

            if (variants.length === 1) {
                return coerceFormValues(value, variants[0]);
            }

            if (typeof value === "string") {
                for (let i = 0; i < variants.length; i += 1) {
                    const variant = variants[i];
                    const variantType = schemaType(variant);
                    if (variantType === "number" || variantType === "integer") {
                        const coercedNumber = coerceNumberString(value, variantType === "integer");
                        if (coercedNumber === undefined || typeof coercedNumber === "number") {
                            return coercedNumber;
                        }
                    }
                    if (variantType === "boolean") {
                        const coercedBoolean = coerceBooleanString(value);
                        if (typeof coercedBoolean === "boolean") {
                            return coercedBoolean;
                        }
                    }
                }
            }

            for (let i = 0; i < variants.length; i += 1) {
                const variant = variants[i];
                const variantType = schemaType(variant);
                if (variantType === "object" && typeof value === "object" && !Array.isArray(value)) {
                    return coerceFormValues(value, variant);
                }
                if (variantType === "array" && Array.isArray(value)) {
                    return coerceFormValues(value, variant);
                }
            }

            return value;
        }

        if (type === "number" || type === "integer") {
            if (typeof value === "string") {
                const coerced = coerceNumberString(value, type === "integer");
                if (coerced === undefined || typeof coerced === "number") {
                    return coerced;
                }
            }
            return value;
        }

        if (type === "boolean") {
            if (typeof value === "string") {
                const coerced = coerceBooleanString(value);
                if (typeof coerced === "boolean") {
                    return coerced;
                }
            }
            return value;
        }

        if (type === "object") {
            if (typeof value !== "object" || Array.isArray(value)) {
                return value;
            }

            const obj = value;
            let props = {};
            if (schema.properties && typeof schema.properties === "object") {
                props = schema.properties;
            }

            let additional = null;
            if (schema.additionalProperties && typeof schema.additionalProperties === "object") {
                additional = schema.additionalProperties;
            }
            const result = {};

            Object.keys(obj).forEach(function (key) {
                const rawValue = obj[key];
                let propSchema = additional;
                if (Object.prototype.hasOwnProperty.call(props, key)) {
                    propSchema = props[key];
                }
                const coerced = propSchema ? coerceFormValues(rawValue, propSchema) : rawValue;
                if (coerced !== undefined) {
                    result[key] = coerced;
                }
            });

            return result;
        }

        if (type === "array") {
            if (!Array.isArray(value)) {
                return value;
            }

            if (Array.isArray(schema.items)) {
                const tupleSchemas = schema.items;
                return value.map(function (item, index) {
                    if (index >= tupleSchemas.length) {
                        return item;
                    }

                    const itemSchema = tupleSchemas[index];
                    if (!itemSchema) {
                        return item;
                    }

                    return coerceFormValues(item, itemSchema);
                });
            }

            const itemsSchema = schema.items;
            if (!itemsSchema) {
                return value;
            }

            return value
                .map(function (item) {
                    return coerceFormValues(item, itemsSchema);
                })
                .filter(function (entry) {
                    return entry !== undefined;
                });
        }

        return value;
    }

    window.BlazeClawConfigFormCoerce = {
        schemaType: schemaType,
        coerceFormValues: coerceFormValues,
    };
})();
