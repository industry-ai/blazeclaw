const path = require("path");
const fs = require("fs");

/**
 * Modify an existing PPTX template using pptx-automizer.
 *
 * Content format:
 * {
 *   "slides": [
 *     {
 *       "slideNumber": 1,
 *       "texts": [
 *         { "shapeName": "Title 1", "text": "New Title" },
 *         { "shapeName": "Content 2", "text": "New Content" }
 *       ],
 *       "replaceTags": [
 *         { "tag": "{{name}}", "text": "John Doe" },
 *         { "tag": "{{date}}", "text": "2024-01-01" }
 *       ],
 *       "setMultiText": [
 *         {
 *           "shapeName": "List 3",
 *           "paragraphs": [
 *             { "text": "Item 1", "bold": true },
 *             { "text": "Item 2", "italic": true }
 *           ]
 *         }
 *       ]
 *     }
 *   ]
 * }
 */
async function modifyPPTX(templatePath, contentJson, outputPath) {
  const Automizer = require("pptx-automizer");
  const { modify, ModifyTextHelper } = Automizer;

  const content = typeof contentJson === "string" ? JSON.parse(contentJson) : contentJson;
  const templateDir = path.dirname(path.resolve(templatePath));
  const templateFile = path.basename(templatePath);

  const automizer = new Automizer({
    templateDir: templateDir,
    outputDir: path.dirname(path.resolve(outputPath)),
    removeExistingSlides: true,
    autoImportSlideMasters: true,
    compression: 0,
    verbosity: 1,
  });

  const pres = automizer.loadRoot(templateFile).load(templateFile, "template");

  const slideConfigs = content.slides || [];
  for (const cfg of slideConfigs) {
    pres.addSlide("template", cfg.slideNumber, (slide) => {
      // Modify by shape name: set text
      if (cfg.texts) {
        for (const t of cfg.texts) {
          slide.modifyElement(t.shapeName, [ModifyTextHelper.setText(t.text)]);
        }
      }

      // Replace tagged text like {{tag}}
      if (cfg.replaceTags) {
        for (const tag of cfg.replaceTags) {
          slide.modifyElement(
            tag.shapeName || tag.elementName,
            modify.replaceText([
              {
                replace: tag.tag,
                by: { text: tag.text },
              },
            ])
          );
        }
      }

      // Set multi-text (styled paragraphs)
      if (cfg.setMultiText) {
        for (const mt of cfg.setMultiText) {
          const paragraphs = mt.paragraphs.map((p) => ({
            paragraph: {
              alignment: p.align || "l",
              bullet: p.bullet || false,
              level: p.level || 0,
              marginLeft: p.marginLeft,
              indent: p.indent,
            },
            textRuns: [
              {
                text: p.text,
                style: {
                  fontSize: p.fontSize,
                  isBold: p.bold,
                  isItalics: p.italic,
                  color: p.color ? { type: "srgbClr", value: p.color.replace("#", "") } : undefined,
                  font: p.fontFace,
                },
              },
            ],
          }));
          slide.modifyElement(mt.shapeName, [modify.setMultiText(paragraphs)]);
        }
      }

      // HTML to multi-text
      if (cfg.htmlContent) {
        for (const hc of cfg.htmlContent) {
          slide.modifyElement(hc.shapeName, [modify.htmlToMultiText(hc.html)]);
        }
      }

      // Set image source
      if (cfg.images) {
        if (!automizer._mediaDir && content.mediaDir) {
          // Media dir handling will be done via loadMedia
        }
      }
    });
  }

  const outName = path.basename(outputPath);
  await pres.write(outName);
  return outputPath;
}

/**
 * Modify using direct ZIP manipulation (for simple text replacement
 * without pptx-automizer overhead if template is simple).
 * Content: { slideNumber: [textArray] } e.g. { 1: ["Title", "Subtitle"] }
 */
async function modifyPPTXDirect(templatePath, contentMap, outputPath) {
  const fs = require("fs");
  const zip = require("jszip");
  const { DOMParser, XMLSerializer } = require("xmldom");

  const data = fs.readFileSync(templatePath);
  const zout = new zip();
  const z = await zip.loadAsync(data);

  const A_NS = "http://schemas.openxmlformats.org/drawingml/2006/main";
  const ns = (tag) => `${A_NS}:${tag}`;

  const outZip = new zip();

  for (const name of Object.keys(z.files)) {
    const file = z.files[name];
    if (file.dir) {
      outZip.file(name, null, { dir: true });
      continue;
    }

    let content = await file.async("nodebuffer");

    const m = name.match(/^ppt\/slides\/slide(\d+)\.xml$/);
    if (m) {
      const slideNum = parseInt(m[1]);
      const texts = contentMap[slideNum];
      if (texts && texts.length > 0) {
        let xmlStr = content.toString("utf-8");
        const parser = new DOMParser();
        const doc = parser.parseFromString(xmlStr, "text/xml");
        const tElements = doc.getElementsByTagNameNS(A_NS, "t");
        let idx = 0;
        let serialIdx = 0;
        const allTexts = [];
        for (let i = 0; i < tElements.length; i++) {
          if (tElements[i].textContent && tElements[i].textContent.trim()) {
            allTexts.push(tElements[i]);
          }
        }
        for (const el of allTexts) {
          if (idx < texts.length) {
            // Preserve whitespace by setting textContent directly
            while (el.firstChild) el.removeChild(el.firstChild);
            el.appendChild(doc.createTextNode(texts[idx]));
            idx++;
          }
        }
        // Ensure normAutofit on each text body
        const bodyPrs = doc.getElementsByTagNameNS(A_NS, "bodyPr");
        for (let i = 0; i < bodyPrs.length; i++) {
          const bp = bodyPrs[i];
          const saf = bp.getElementsByTagNameNS(A_NS, "spAutoFit");
          while (saf.length > 0) bp.removeChild(saf[0]);
          const naf = bp.getElementsByTagNameNS(A_NS, "normAutofit");
          if (naf.length === 0) {
            const newNaf = doc.createElementNS(A_NS, "a:normAutofit");
            bp.appendChild(newNaf);
          }
        }
        const serializer = new XMLSerializer();
        content = Buffer.from(serializer.serializeToString(doc), "utf-8");
      }
    }

    outZip.file(name, content);
  }

  const outBuffer = await outZip.generateAsync({ type: "nodebuffer", compression: "DEFLATE" });
  fs.writeFileSync(outputPath, outBuffer);
  return outputPath;
}

module.exports = { modifyPPTX, modifyPPTXDirect };
