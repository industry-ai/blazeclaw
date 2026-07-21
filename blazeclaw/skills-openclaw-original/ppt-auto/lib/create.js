const PptxGenJS = require("pptxgenjs");

function hexToArgb(hex) {
  if (!hex || hex === "none") return undefined;
  return hex.replace("#", "");
}

function parseColor(val) {
  if (!val) return undefined;
  return hexToArgb(val);
}

/**
 * Create PPTX from JSON content definition.
 * Content format:
 * {
 *   "title": "Presentation Title",
 *   "slides": [
 *     {
 *       "texts": [
 *         { "text": "Hello", "x": 1, "y": 1, "w": 8, "h": 1.5, "fontSize": 36, "bold": true, "color": "#333333", "align": "center" }
 *       ],
 *       "images": [
 *         { "path": "./image.png", "x": 1, "y": 2, "w": 6, "h": 4 }
 *       ],
 *       "shapes": [
 *         { "type": "rect", "x": 0, "y": 0, "w": 10, "h": 0.5, "fill": "#4472C4" }
 *       ]
 *     }
 *   ]
 * }
 */
async function createPPTX(contentJson, outputPath) {
  const pres = new PptxGenJS();
  const content = typeof contentJson === "string" ? JSON.parse(contentJson) : contentJson;

  if (content.title) pres.title = content.title;
  if (content.author) pres.author = content.author;
  if (content.subject) pres.subject = content.subject;

  const slides = content.slides || [];
  for (const slideData of slides) {
    const slide = pres.addSlide();

    // Background color
    if (slideData.bgColor) {
      slide.background = { color: hexToArgb(slideData.bgColor) };
    }

    // Shapes (rectangles, ellipses, etc.)
    if (slideData.shapes) {
      for (const shape of slideData.shapes) {
        const opts = {
          x: shape.x || 0,
          y: shape.y || 0,
          w: shape.w || 2,
          h: shape.h || 2,
          fill: parseColor(shape.fill),
          line: parseColor(shape.line),
          lineSize: shape.lineSize,
        };
        if (shape.type === "ellipse") {
          slide.addShape(pres.ShapeType.ellipse, opts);
        } else if (shape.type === "roundRect") {
          opts.rectRadius = shape.radius || 0.1;
          slide.addShape(pres.ShapeType.roundRect, opts);
        } else {
          slide.addShape(pres.ShapeType.rect, opts);
        }
      }
    }

    // Text boxes
    if (slideData.texts) {
      for (const text of slideData.texts) {
        const opts = {
          x: text.x || 0,
          y: text.y || 0,
          w: text.w || 8,
          h: text.h || 1,
          fontSize: text.fontSize || 18,
          fontFace: text.fontFace || "Microsoft YaHei",
          color: parseColor(text.color) || "363636",
          bold: text.bold || false,
          italic: text.italic || false,
          underline: text.underline || false,
          align: text.align || "left",
          valign: text.valign || "top",
          fill: parseColor(text.bgColor),
          line: parseColor(text.borderColor),
          lineSize: text.borderSize,
          margin: text.margin || [0, 0, 0, 0],
          autoFit: text.autoFit,
        };
        if (text.breakLine) {
          const lines = text.text.split("\n");
          const textObjs = lines.map((line, i) => {
            const obj = { text: line, options: { ...opts } };
            if (i < lines.length - 1) obj.options.breakLine = true;
            return obj;
          });
          slide.addText(textObjs, opts);
        } else {
          slide.addText(text.text, opts);
        }
      }
    }

    // Images
    if (slideData.images) {
      for (const img of slideData.images) {
        if (require("fs").existsSync(img.path)) {
          slide.addImage({
            path: img.path,
            x: img.x || 0,
            y: img.y || 0,
            w: img.w || 4,
            h: img.h || 3,
            sizing: img.sizing || { type: "contain", w: img.w || 4, h: img.h || 3 },
          });
        }
      }
    }

    // Tables
    if (slideData.tables) {
      for (const table of slideData.tables) {
        const headerOpts = {
          fill: parseColor(table.headerFill) || "4472C4",
          color: parseColor(table.headerColor) || "FFFFFF",
          bold: true,
          fontSize: table.fontSize || 14,
          fontFace: table.fontFace || "Microsoft YaHei",
          align: "center",
          valign: "middle",
        };
        const cellOpts = {
          fill: parseColor(table.cellFill) || "F2F2F2",
          color: parseColor(table.cellColor) || "333333",
          fontSize: table.fontSize || 12,
          fontFace: table.fontFace || "Microsoft YaHei",
          align: "center",
          valign: "middle",
          border: { type: "solid", pt: 1, color: "CCCCCC" },
        };
        const rows = [];
        if (table.headers) {
          rows.push(table.headers.map((h) => ({ text: h, options: headerOpts })));
        }
        if (table.rows) {
          for (const row of table.rows) {
            rows.push(row.map((c) => ({ text: c, options: cellOpts })));
          }
        }
        slide.addTable(rows, {
          x: table.x || 0.5,
          y: table.y || 0.5,
          w: table.w || 9,
          colW: table.colW || undefined,
          rowH: table.rowH || 0.5,
          margin: [2, 4, 2, 4],
        });
      }
    }

    // Charts
    if (slideData.charts) {
      for (const chart of slideData.charts) {
        const chartData = [];
        for (const series of chart.series || []) {
          chartData.push({
            name: series.name,
            labels: series.labels || [],
            values: series.values || [],
          });
        }
        const chartOpts = {
          x: chart.x || 1,
          y: chart.y || 1,
          w: chart.w || 8,
          h: chart.h || 4.5,
          showLegend: chart.showLegend !== false,
          showTitle: chart.showTitle || false,
          catAxisLabelColor: chart.labelColor || "666666",
          catAxisLabelFontSize: chart.labelFontSize || 10,
        };
        const typeMap = {
          bar: pres.ChartType.bar,
          column: pres.ChartType.column,
          line: pres.ChartType.line,
          pie: pres.ChartType.pie,
          area: pres.ChartType.area,
          scatter: pres.ChartType.scatter,
          radar: pres.ChartType.radar,
        };
        const chartType = typeMap[chart.type] || pres.ChartType.column;
        slide.addChart(chartType, chartData, chartOpts);
      }
    }
  }

  await pres.writeFile({ fileName: outputPath });
  return outputPath;
}

module.exports = { createPPTX };
