const path = require("path");
const fs = require("fs");
const { createPPTX } = require("./lib/create");
const { modifyPPTX, modifyPPTXDirect } = require("./lib/modify");
const { convertToHTML } = require("./lib/convert");
const { generateHomeworkPPT } = require("./lib/homework");

const SKILL_DIR = __dirname;

function printUsage() {
  console.log(`
PPT Auto — PPT自动化工具 v1.0
===============================
用法:
  node index.js create <内容JSON文件> <输出.pptx>
      根据内容描述创建PPT（使用 PptxGenJS）

  node index.js modify <模板.pptx> <内容JSON文件> <输出.pptx>
      修改PPT模板中的文字内容（使用 pptx-automizer）

  node index.js modify-direct <模板.pptx> <内容JSON文件> <输出.pptx>
      通过ZIP直操作快速替换文字（无需模板结构，性能快）

  node index.js convert <输入.pptx> [输出目录]
      将PPTX转换为交互式HTML页面（使用 node-pptx-parser + jsdom）

  node index.js full <模板.pptx> <内容JSON文件> <输出.pptx> [HTML输出目录]
      先修改模板内容，再自动转为HTML，一次性完成

内容JSON格式:
{
  "title": "演示标题",
  "author": "作者",
  "slides": [
    {
      "slideNumber": 1,
      "texts": [
        { "text": "新标题", "x": 1, "y": 0.5, "w": 8, "h": 1.5, "fontSize": 36, "bold": true, "color": "#333333", "align": "center" }
      ],
      "images": [ { "path": "./img.png", "x": 1, "y": 2, "w": 6, "h": 4 } ],
      "shapes": [ { "type": "rect", "x": 0, "y": 0, "w": 10, "h": 0.5, "fill": "#4472C4" } ],
      "bgColor": "#FFFFFF"
    }
  ]
}

修改模式内容JSON格式:
{
  "slides": [
    {
      "slideNumber": 1,
      "texts": [
        { "shapeName": "标题占位符名称", "text": "新文本" }
      ],
      "replaceTags": [
        { "shapeName": "元素名", "tag": "{{占位符}}", "text": "替换值" }
      ]
    }
  ]
}

direct模式内容JSON格式:
{
  "1": ["第1个文本", "第2个文本", ...],
  "2": ["第1个文本", ...]
}
`);
}

async function main() {
  const cmd = process.argv[2];

  if (!cmd || cmd === "--help" || cmd === "-h") {
    printUsage();
    return;
  }

  switch (cmd) {
    case "create": {
      if (process.argv.length < 5) {
        console.error("用法: node index.js create <内容JSON文件> <输出.pptx>");
        process.exit(1);
      }
      const contentFile = path.resolve(process.argv[3]);
      const outputFile = path.resolve(process.argv[4]);
      if (!fs.existsSync(contentFile)) {
        console.error(`内容文件不存在: ${contentFile}`);
        process.exit(1);
      }
      const content = JSON.parse(fs.readFileSync(contentFile, "utf-8"));
      console.log("正在创建PPT...");
      const result = await createPPTX(content, outputFile);
      console.log(`PPT已创建: ${result}`);
      break;
    }

    case "modify": {
      if (process.argv.length < 6) {
        console.error("用法: node index.js modify <模板.pptx> <内容JSON文件> <输出.pptx>");
        process.exit(1);
      }
      const templateFile = path.resolve(process.argv[3]);
      const contentFile = path.resolve(process.argv[4]);
      const outputFile = path.resolve(process.argv[5]);
      if (!fs.existsSync(templateFile)) {
        console.error(`模板文件不存在: ${templateFile}`);
        process.exit(1);
      }
      if (!fs.existsSync(contentFile)) {
        console.error(`内容文件不存在: ${contentFile}`);
        process.exit(1);
      }
      const content = JSON.parse(fs.readFileSync(contentFile, "utf-8"));
      console.log("正在修改PPT...");
      const result = await modifyPPTX(templateFile, content, outputFile);
      console.log(`PPT已修改: ${result}`);
      break;
    }

    case "modify-direct": {
      if (process.argv.length < 6) {
        console.error("用法: node index.js modify-direct <模板.pptx> <内容JSON文件> <输出.pptx>");
        process.exit(1);
      }
      const templateFile = path.resolve(process.argv[3]);
      const contentFile = path.resolve(process.argv[4]);
      const outputFile = path.resolve(process.argv[5]);
      if (!fs.existsSync(templateFile)) {
        console.error(`模板文件不存在: ${templateFile}`);
        process.exit(1);
      }
      if (!fs.existsSync(contentFile)) {
        console.error(`内容文件不存在: ${contentFile}`);
        process.exit(1);
      }
      const content = JSON.parse(fs.readFileSync(contentFile, "utf-8"));
      console.log("正在快速修改PPT...");
      const result = await modifyPPTXDirect(templateFile, content, outputFile);
      console.log(`PPT已修改: ${result}`);
      break;
    }

    case "convert": {
      if (process.argv.length < 4) {
        console.error("用法: node index.js convert <输入.pptx> [输出目录]");
        process.exit(1);
      }
      const inputFile = path.resolve(process.argv[3]);
      const outputDir = process.argv[4] ? path.resolve(process.argv[4]) : null;
      if (!fs.existsSync(inputFile)) {
        console.error(`PPT文件不存在: ${inputFile}`);
        process.exit(1);
      }
      console.log("正在转为HTML...");
      const result = await convertToHTML(inputFile, outputDir);
      console.log(`HTML已生成: ${result.htmlPath}`);
      console.log(`资源目录: ${result.outputDir}`);
      break;
    }

    case "full": {
      if (process.argv.length < 6) {
        console.error("用法: node index.js full <模板.pptx> <内容JSON文件> <输出.pptx> [HTML输出目录]");
        process.exit(1);
      }
      const templateFile = path.resolve(process.argv[3]);
      const contentFile = path.resolve(process.argv[4]);
      const outputFile = path.resolve(process.argv[5]);
      const htmlDir = process.argv[6] ? path.resolve(process.argv[6]) : null;
      if (!fs.existsSync(templateFile)) {
        console.error(`模板文件不存在: ${templateFile}`);
        process.exit(1);
      }
      if (!fs.existsSync(contentFile)) {
        console.error(`内容文件不存在: ${contentFile}`);
        process.exit(1);
      }
      const content = JSON.parse(fs.readFileSync(contentFile, "utf-8"));
      console.log("=== 步骤1: 修改PPT ===");
      await modifyPPTX(templateFile, content, outputFile);
      console.log(`PPT已修改: ${outputFile}`);
      console.log("=== 步骤2: 转为HTML ===");
      const result = await convertToHTML(outputFile, htmlDir);
      console.log(`HTML已生成: ${result.htmlPath}`);
      console.log(`资源目录: ${result.outputDir}`);
      break;
    }

    case "homework": {
      if (process.argv.length < 4) {
        console.error("用法: node index.js homework <作业URL> [模版路径]");
        process.exit(1);
      }
      const homeworkUrl = process.argv[3];
      const templatePath = process.argv[4] || null;
      const outputDir = path.join(SKILL_DIR, "templates");
      console.log("=== 开始制作作业讲解PPT ===");
      const res = await generateHomeworkPPT(homeworkUrl, templatePath, outputDir);
      console.log("PPT: " + res.pptxPath);
      console.log("HTML: " + res.htmlPath);
      console.log("工作目录: " + res.workDir);
      break;
    }

    default:
      console.error(`未知命令: ${cmd}`);
      printUsage();
      process.exit(1);
  }
}

main().catch((err) => {
  console.error("错误:", err.message);
  process.exit(1);
});
