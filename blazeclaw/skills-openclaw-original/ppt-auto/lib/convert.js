const path = require("path");
const fs = require("fs");

const PPT_W = 12192000;
const PPT_H = 6858000;

const SCHEME_MAP = {
  "bg1": "#ffffff", "bg2": "#e7e6e6", "tx1": "#000000", "tx2": "#44546a",
  "accent1": "#4472c4", "accent2": "#ed7d31", "accent3": "#a5a5a5",
  "accent4": "#ffc000", "accent5": "#5b9bd5", "accent6": "#70ad47",
  "dk1": "#000000", "dk2": "#44546a", "lt1": "#ffffff", "lt2": "#e7e6e6",
  "hlink": "#0563c1", "folHlink": "#954f72",
};

const FONT_CDN_MAP = {
  "Alibaba PuHuiTi Light": "https://cdn.jsdelivr.net/npm/@fontpkg/alibaba-puhuiti-3-0@0.0.0/AlibabaPuHuiTi-3-45-Light.ttf",
  "阿里巴巴普惠体 Light": "https://cdn.jsdelivr.net/npm/@fontpkg/alibaba-puhuiti-3-0@0.0.0/AlibabaPuHuiTi-3-45-Light.ttf",
  "Alibaba PuHuiTi": "https://cdn.jsdelivr.net/npm/@fontpkg/alibaba-puhuiti-3-0@0.0.0/AlibabaPuHuiTi-3-55-Regular.ttf",
  "阿里巴巴普惠体": "https://cdn.jsdelivr.net/npm/@fontpkg/alibaba-puhuiti-3-0@0.0.0/AlibabaPuHuiTi-3-55-Regular.ttf",
  "Alibaba PuHuiTi Medium": "https://cdn.jsdelivr.net/npm/@fontpkg/alibaba-puhuiti-3-0@0.0.0/AlibabaPuHuiTi-3-65-Medium.ttf",
  "阿里巴巴普惠体 Medium": "https://cdn.jsdelivr.net/npm/@fontpkg/alibaba-puhuiti-3-0@0.0.0/AlibabaPuHuiTi-3-65-Medium.ttf",
  "Alibaba PuHuiTi Bold": "https://cdn.jsdelivr.net/npm/@fontpkg/alibaba-puhuiti-3-0@0.0.0/AlibabaPuHuiTi-3-85-Bold.ttf",
  "阿里巴巴普惠体 Bold": "https://cdn.jsdelivr.net/npm/@fontpkg/alibaba-puhuiti-3-0@0.0.0/AlibabaPuHuiTi-3-85-Bold.ttf",
};

function genFontFaces(usedFonts) {
  const out = [];
  const seen = new Set();
  for (const name of usedFonts) {
    const url = FONT_CDN_MAP[name];
    if (url && !seen.has(url)) {
      seen.add(url);
      out.push(`@font-face{font-family:"${name}";src:url(${url}) format("truetype");font-display:swap;}`);
      // Also register under the first Latin name from FONT_MAP so CSS font-family matches
      const mapped = FONT_MAP[name];
      if (mapped) {
        const latinName = mapped.match(/"([^"]+)"/);
        if (latinName && latinName[1] !== name) {
          out.push(`@font-face{font-family:"${latinName[1]}";src:url(${url}) format("truetype");font-display:swap;}`);
        }
      }
    }
  }
  return out.join("\n");
}

const FONT_MAP = {
  "站酷快乐体2016修订版": '"ZCOOL KuaiLe","KaiTi","STKaiti",cursive',
  "Source Han Serif CN Heavy": '"Source Han Serif SC","Noto Serif SC","Noto Serif","SimSun","STSong",serif',
  "Source Han Serif CN Regular": '"Source Han Serif SC","Noto Serif SC","Noto Serif","SimSun","STSong",serif',
  "Source Han Sans CN Regular": '"Source Han Sans SC","Noto Sans SC","Noto Sans","Microsoft YaHei","PingFang SC","Hiragino Sans GB",sans-serif',
  "Source Han Sans CN Medium": '"Source Han Sans SC","Noto Sans SC","Noto Sans","Microsoft YaHei","PingFang SC","Hiragino Sans GB",sans-serif',
  "思源宋体 CN Heavy": '"Source Han Serif SC","Noto Serif SC","Noto Serif","SimSun","STSong",serif',
  "思源宋体 CN Regular": '"Source Han Serif SC","Noto Serif SC","Noto Serif","SimSun","STSong",serif',
  "思源黑体 CN Regular": '"Source Han Sans SC","Noto Sans SC","Noto Sans","Microsoft YaHei","PingFang SC","Hiragino Sans GB",sans-serif',
  "思源黑体 CN Medium": '"Source Han Sans SC","Noto Sans SC","Noto Sans","Microsoft YaHei","PingFang SC","Hiragino Sans GB",sans-serif',
  "等线": '"DengXian","Microsoft YaHei","PingFang SC","Hiragino Sans GB",sans-serif',
  "等线 Light": '"DengXian Light","DengXian","Microsoft YaHei Light","Microsoft YaHei","PingFang SC",sans-serif',
  "阿里巴巴普惠体": '"Alibaba PuHuiTi","Alibaba PuHuiTi Light","Microsoft YaHei","PingFang SC","Hiragino Sans GB",sans-serif',
  "阿里巴巴普惠体 Light": '"Alibaba PuHuiTi Light","Alibaba PuHuiTi","Microsoft YaHei Light","Microsoft YaHei","PingFang SC","Hiragino Sans GB",sans-serif',
  "Alibaba PuHuiTi": '"Alibaba PuHuiTi","Alibaba PuHuiTi Light","Microsoft YaHei","PingFang SC","Hiragino Sans GB",sans-serif',
  "Alibaba PuHuiTi Light": '"Alibaba PuHuiTi Light","Alibaba PuHuiTi","Microsoft YaHei Light","Microsoft YaHei","PingFang SC","Hiragino Sans GB",sans-serif',
  "演示镇魂行楷": '"YanShi ZhenHunXingKai","STKaiti","KaiTi",cursive',
  "YanShi ZhenHunXingKai": '"YanShi ZhenHunXingKai","STKaiti","KaiTi",cursive',
  "汉仪润圆-65简": '"ZCOOL KuaiLe","Microsoft YaHei","PingFang SC","Hiragino Sans GB",sans-serif',
  "汉仪君黑-45简": '"Noto Sans SC","Microsoft YaHei","PingFang SC","Hiragino Sans GB",sans-serif',
};

function emuPct(emu, total) {
  return total > 0 ? ((emu / total) * 100).toFixed(4) : "0";
}

function rgbToHsl(r, g, b) {
  r /= 255; g /= 255; b /= 255;
  const mx = Math.max(r, g, b), mn = Math.min(r, g, b);
  let h = 0, s = 0, l = (mx + mn) / 2;
  if (mx !== mn) {
    const d = mx - mn;
    s = d / (1 - Math.abs(2 * l - 1));
    if (mx === r) h = ((g - b) / d) % 6;
    else if (mx === g) h = (b - r) / d + 2;
    else h = (r - g) / d + 4;
    h /= 6;
  }
  return [h, s, l];
}

function hslToRgb(h, s, l) {
  if (s === 0) { const v = Math.round(l * 255); return [v, v, v]; }
  const q = l < 0.5 ? l * (1 + s) : l + s - l * s;
  const p = 2 * l - q;
  const hue2rgb = (t) => {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1/6) return p + (q - p) * 6 * t;
    if (t < 1/2) return q;
    if (t < 2/3) return p + (q - p) * (2/3 - t) * 6;
    return p;
  };
  return [Math.round(hue2rgb(h + 1/3) * 255), Math.round(hue2rgb(h) * 255), Math.round(hue2rgb(h - 1/3) * 255)];
}

function applyColorTransforms(hex, lumMod, lumOff) {
  if (!lumMod && !lumOff) return hex;
  let r = parseInt(hex.slice(1, 3), 16);
  let g = parseInt(hex.slice(3, 5), 16);
  let b = parseInt(hex.slice(5, 7), 16);
  let [h, s, l] = rgbToHsl(r, g, b);
  if (lumMod) l *= parseInt(lumMod) / 100000;
  if (lumOff) l += parseInt(lumOff) / 100000;
  l = Math.max(0, Math.min(1, l));
  [r, g, b] = hslToRgb(h, s, l);
  return "#" + [r, g, b].map(v => v.toString(16).padStart(2, "0")).join("");
}

function resolveFont(fontName, themeFonts) {
  if (!fontName) return null;
  if (themeFonts[fontName]) return themeFonts[fontName];
  return fontName;
}

function cssFont(fontName, themeFonts) {
  const fn = resolveFont(fontName, themeFonts);
  if (!fn) return null;
  if (FONT_MAP[fn]) return FONT_MAP[fn];
  return `"${fn}","Microsoft YaHei","PingFang SC",sans-serif`;
}

function escHtml(s) {
  return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
}

function decodeXml(s) {
  return s.replace(/&amp;/g, "&").replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/&quot;/g, '"').replace(/&apos;/g, "'");
}

function normalizeXmlNs(xml) {
  const prefixMap = {};
  const nsRegex = /xmlns:(\w+)="([^"]+)"/g;
  let m;
  while ((m = nsRegex.exec(xml)) !== null) {
    const prefix = m[1];
    const uri = m[2];
    let standard;
    if (uri === "http://schemas.openxmlformats.org/presentationml/2006/main") standard = "p";
    else if (uri === "http://schemas.openxmlformats.org/drawingml/2006/main") standard = "a";
    else if (uri === "http://schemas.openxmlformats.org/officeDocument/2006/relationships") standard = "r";
    if (standard && prefix !== standard) prefixMap[prefix] = standard;
  }
  if (Object.keys(prefixMap).length === 0) return xml;
  for (const [oldPref, newPref] of Object.entries(prefixMap)) {
    xml = xml.split(oldPref + ":").join(newPref + ":");
  }
  return xml;
}

function findColor(xmlSnippet) {
  // rgb color
  const srgb = xmlSnippet.match(/<a:srgbClr\s+val="([^"]+)"([^>]*>)/);
  if (srgb) {
    let hex = "#" + srgb[1];
    const rest = srgb[2];
    const lumMod = rest.match(/<a:lumMod\s+val="(\d+)"/);
    const lumOff = rest.match(/<a:lumOff\s+val="(\d+)"/);
    // also search past the closing of srgbClr tag
    const fullMatch = xmlSnippet.match(/<a:srgbClr[^>]*val="([^"]+)"[\s\S]*?(?:<\/a:srgbClr>|$)/);
    if (fullMatch) {
      const lumMod2 = fullMatch[0].match(/<a:lumMod\s+val="(\d+)"/);
      const lumOff2 = fullMatch[0].match(/<a:lumOff\s+val="(\d+)"/);
      if (lumMod2 || lumOff2) return applyColorTransforms(hex, lumMod2?.[1], lumOff2?.[1]);
    }
    return hex;
  }
  // scheme color
  const sch = xmlSnippet.match(/<a:schemeClr\s+val="([^"]+)"([\s\S]*?)(?:<\/a:schemeClr>|$)/);
  if (sch) {
    const base = SCHEME_MAP[sch[1]];
    if (!base) return null;
    const lumMod = sch[2].match(/<a:lumMod\s+val="(\d+)"/);
    const lumOff = sch[2].match(/<a:lumOff\s+val="(\d+)"/);
    if (lumMod || lumOff) return applyColorTransforms(base, lumMod?.[1], lumOff?.[1]);
    return base;
  }
  return null;
}

function findSolidFill(xmlSnippet) {
  const m = xmlSnippet.match(/<a:solidFill>([\s\S]*?)<\/a:solidFill>/);
  if (!m) return null;
  return findColor(m[1]);
}

function findShapeFill(xml) {
  const spPr = xml.match(/<p:spPr>([\s\S]*?)<\/p:spPr>/);
  if (!spPr) return null;
  return findSolidFill(spPr[1]);
}

function parseBgColor(xml) {
  // Try <p:bg> (slide/layout) or <p:bgPr> (master)
  let m = xml.match(/<p:bg>([\s\S]*?)<\/p:bg>/);
  if (!m) m = xml.match(/<p:bgPr>([\s\S]*?)<\/p:bgPr>/);
  if (!m) return null;
  return findColor(m[1]);
}

function parseBgImage(xml, relMap) {
  // Try <p:bg> (slide/layout) or <p:bgPr> (master)
  let bg = xml.match(/<p:bg>[\s\S]*?<\/p:bg>/);
  if (!bg) bg = xml.match(/<p:bgPr>[\s\S]*?<\/p:bgPr>/);
  if (!bg) return null;
  const m = bg[0].match(/r:embed="([^"]+)"/);
  if (!m || !relMap[m[1]]) return null;
  return path.basename(relMap[m[1]]);
}

function getXfrm(xml) {
  const m = xml.match(/<a:xfrm[^>]*>[\s\S]*?<a:off\s+x="(-?\d+)"\s+y="(-?\d+)"\s*\/>[\s\S]*?<a:ext\s+cx="(-?\d+)"\s+cy="(-?\d+)"\s*\/>/);
  if (!m) return null;
  return { x: parseInt(m[1]), y: parseInt(m[2]), cx: parseInt(m[3]), cy: parseInt(m[4]) };
}

function getGrpChOff(xml) {
  const ch = xml.match(/<a:chOff\s+x="(-?\d+)"\s+y="(-?\d+)"\s*\/>/);
  if (!ch) return null;
  const off = xml.match(/<a:off\s+x="(-?\d+)"\s+y="(-?\d+)"\s*\/>/);
  return {
    x: off ? parseInt(off[1]) : 0,
    y: off ? parseInt(off[2]) : 0,
    chOffX: parseInt(ch[1]),
    chOffY: parseInt(ch[2]),
  };
}

function loadThemeFonts(zip) {
  const fonts = {};
  const themeNames = Object.keys(zip.files).filter(n => n.startsWith("ppt/theme/") && n.endsWith(".xml")).sort();
  for (const name of themeNames) {
    const xml = zip.files[name].async ? null : null;
    // We'll parse via string
  }
  for (const name of themeNames) {
    const xml = zip.files[name].async ? null : null;
  }
  // Use sync-like approach via async
  return fonts;
}

async function loadThemeFontsAsync(zip) {
  const fonts = {};
  const themeNames = Object.keys(zip.files).filter(n => n.startsWith("ppt/theme/") && n.endsWith(".xml")).sort();
  for (const name of themeNames) {
    let xml = await zip.files[name].async("text");
    xml = normalizeXmlNs(xml);
    const majorLatin = xml.match(/<a:majorFont>[\s\S]*?<a:latin\s+typeface="([^"]+)"/);
    const majorEa = xml.match(/<a:majorFont>[\s\S]*?<a:ea\s+typeface="([^"]+)"/);
    const minorLatin = xml.match(/<a:minorFont>[\s\S]*?<a:latin\s+typeface="([^"]+)"/);
    const minorEa = xml.match(/<a:minorFont>[\s\S]*?<a:ea\s+typeface="([^"]+)"/);
    if (majorLatin) fonts["+mj-lt"] = majorLatin[1];
    if (majorEa) fonts["+mj-ea"] = majorEa[1];
    if (!fonts["+mj-lt"] && majorLatin) fonts["+mj-lt"] = majorLatin[1];
    if (minorLatin) fonts["+mn-lt"] = minorLatin[1];
    if (minorEa) fonts["+mn-ea"] = minorEa[1];
    if (!fonts["+mn-lt"] && minorLatin) fonts["+mn-lt"] = minorLatin[1];
  }
  return fonts;
}

function getRelsMap(zip, basePath) {
  const parts = basePath.split("/");
  const filename = parts.pop();
  const parent = parts.join("/");
  const relsPath = parent + "/_rels/" + filename + ".rels";
  const relMap = {};
  if (zip.files[relsPath]) {
    // Need async
    return null; // handled differently
  }
  return relMap;
}

async function getRelsMapAsync(zip, basePath) {
  const parts = basePath.split("/");
  const filename = parts.pop();
  const parent = parts.join("/");
  const relsPath = parent + "/_rels/" + filename + ".rels";
  const relMap = {};
  if (zip.files[relsPath]) {
    const xml = await zip.files[relsPath].async("text");
    const relRegex = /<Relationship\s+Id="([^"]+)"[^>]*?Target="([^"]+)"/g;
    let m;
    while ((m = relRegex.exec(xml)) !== null) {
      relMap[m[1]] = m[2];
    }
  }
  return relMap;
}

function resolveRelTarget(rId, relMap, baseDir) {
  const target = relMap[rId];
  if (!target) return null;
  if (target.startsWith("../")) {
    let t = target;
    let parts = baseDir.split("/");
    while (t.startsWith("../")) {
      t = t.slice(3);
      parts.pop();
    }
    return parts.join("/") + "/" + t;
  }
  if (target.startsWith("/")) return target.slice(1);
  return baseDir + "/" + target;
}

function getSlideLayoutInfo(slideXml) {
  const m = slideXml.match(/p:sldLayoutId\s+id="\d+"\s+r:id="([^"]+)"/);
  return m ? m[1] : null;
}

function extractTextRuns(txBodyXml, themeFonts) {
  const paragraphs = [];
  const pRegex = /<a:p>[\s\S]*?<\/a:p>/g;
  let pMatch;
  while ((pMatch = pRegex.exec(txBodyXml)) !== null) {
    const pXml = pMatch[0];

    // Default paragraph props (defRPr and endParaRPr)
    let defaultSz = null, defaultBold = false, defaultItalic = false;
    let defaultColor = null, defaultLatin = null, defaultEa = null, defaultSpc = null;
    const defRPr = pXml.match(/<a:defRPr[\s\S]*?<\/a:defRPr>/);
    const endRPr = pXml.match(/<a:endParaRPr[^>]*\/?>/) || pXml.match(/<a:endParaRPr[^>]*><\/a:endParaRPr>/);
    const defSources = [defRPr, endRPr].filter(Boolean);
    for (const src of defSources) {
      const s = src[0];
      const szM = s.match(/sz="(\d+)"/);
      if (szM && defaultSz === null) defaultSz = parseInt(szM[1]) / 100;
      if (/b="1"/.test(s)) defaultBold = true;
      if (/i="1"/.test(s)) defaultItalic = true;
      const c = findColor(s);
      if (c && defaultColor === null) defaultColor = c;
      const latin = s.match(/<a:latin\s+typeface="([^"]+)"/);
      if (latin && defaultLatin === null) defaultLatin = resolveFont(latin[1], themeFonts);
      const ea = s.match(/<a:ea\s+typeface="([^"]+)"/);
      if (ea && defaultEa === null) defaultEa = resolveFont(ea[1], themeFonts);
      const spcM = s.match(/spc="(\d+)"/);
      if (spcM && defaultSpc === null) defaultSpc = parseInt(spcM[1]) / 100;
    }

    // Paragraph alignment
    const algnM = pXml.match(/<a:pPr[^>]*\salgn="([^"]+)"/);
    let align = "center";
    if (algnM) align = { l: "left", ctr: "center", r: "right", just: "justify" }[algnM[1]] || "center";

    // Line spacing
    let lineSp = null;
    const lnSpc = pXml.match(/<a:lnSpc>[\s\S]*?<a:spcPct\s+val="(\d+)"/);
    if (lnSpc) lineSp = parseInt(lnSpc[1]) / 100000;

    // Text runs
    const runs = [];
    const rRegex = /<a:r>[\s\S]*?<\/a:r>/g;
    let rMatch;
    while ((rMatch = rRegex.exec(pXml)) !== null) {
      const rXml = rMatch[0];
      const tM = rXml.match(/<a:t[^>]*>([^<]*)<\/a:t>/);
      if (!tM) continue;
      const text = decodeXml(tM[1]);
      if (!text.trim() && !text.includes("\n")) continue;

      const rPr = rXml.match(/<a:rPr[\s\S]*?<\/a:rPr>/);
      let fontSize = defaultSz || 18;
      let bold = defaultBold;
      let italic = defaultItalic;
      let color = defaultColor || "#333333";
      let latinFont = defaultLatin;
      let eaFont = defaultEa;
      let spc = defaultSpc;

      if (rPr) {
        const szM = rPr[0].match(/sz="(\d+)"/);
        if (szM) fontSize = parseInt(szM[1]) / 100;
        if (/b="1"/.test(rPr[0])) bold = true;
        if (/i="1"/.test(rPr[0])) italic = true;
        const c = findColor(rPr[0]);
        if (c) color = c;
        const latin = rPr[0].match(/<a:latin\s+typeface="([^"]+)"/);
        if (latin) latinFont = resolveFont(latin[1], themeFonts);
        const ea = rPr[0].match(/<a:ea\s+typeface="([^"]+)"/);
        if (ea) eaFont = resolveFont(ea[1], themeFonts);
        const spcM = rPr[0].match(/spc="(\d+)"/);
        if (spcM) spc = parseInt(spcM[1]) / 100;
      }

      if (!latinFont && !eaFont) {
        latinFont = themeFonts["+mn-lt"] || themeFonts["+mj-lt"] || null;
        eaFont = themeFonts["+mn-ea"] || themeFonts["+mj-ea"] || null;
      }

      runs.push({ text, fontSize, bold, italic, color, latinFont, eaFont, spc });
    }

    if (runs.length > 0) {
      paragraphs.push({ runs, align, lineSp });
    }
  }
  return paragraphs;
}

function getShapePreset(xml) {
  const m = xml.match(/<a:prstGeom[^>]*\s+prst="([^"]+)"/);
  return m ? m[1] : null;
}

function extractShape(xml, themeFonts, ox, oy) {
  const xfrm = getXfrm(xml);
  if (!xfrm) return null;

  // Skip placeholders
  if (/<p:nvSpPr>[\s\S]*?<p:nvPr>[\s\S]*?<p:ph\s/m.test(xml)) return null;

  // Text shape
  const txBody = xml.match(/<p:txBody>([\s\S]*?)<\/p:txBody>/);
  if (txBody) {
    const paragraphs = extractTextRuns(txBody[1], themeFonts);
    if (paragraphs.length > 0) {
      const result = {
        type: "text",
        x: xfrm.x + ox, y: xfrm.y + oy,
        cx: xfrm.cx, cy: xfrm.cy,
        paragraphs,
      };

      // Body properties
      const bodyPr = xml.match(/<a:bodyPr[\s\S]*?<\/a:bodyPr>/) || xml.match(/<a:bodyPr[\s\S]*?\/>/) || xml.match(/<a:bodyPr[\s\S]*?>/);
      if (bodyPr) {
        const bp = bodyPr[0];
        ["lIns", "rIns", "tIns", "bIns"].forEach(attr => {
          const m = bp.match(new RegExp(attr + '="(\\d+)"'));
          if (m) result[attr] = parseInt(m[1]);
        });
        const anc = bp.match(/anchor="([^"]+)"/);
        if (anc) result.anchor = anc[1];
        result.normAutofit = /<a:normAutofit\/>/.test(bp) || /normAutofit/.test(xml.substring(0, xml.indexOf('>') + 1000));
      }

      // Collect used font names from all runs
      const usedFonts = [];
      for (const p of paragraphs) {
        for (const r of p.runs) {
          const fn = r.latinFont || r.eaFont;
          if (fn && !usedFonts.includes(fn)) usedFonts.push(fn);
        }
      }
      if (usedFonts.length > 0) result.usedFonts = usedFonts;

      // Shape fill (background) — only check spPr
      const fill = findShapeFill(xml);
      if (fill) {
        result.bgColor = fill;
        const alphaM = xml.match(/<a:alpha\s+val="(\d+)"/);
        if (alphaM) result.bgOpacity = parseInt(alphaM[1]) / 100000;
      }

      return result;
    }
    // txBody has no text runs — fall through to check for solidFill shape
  }

  // Filled shape (no text, or empty txBody with solidFill)
  const fill = findShapeFill(xml);
  if (fill) {
    const result = {
      type: "fill",
      x: xfrm.x + ox, y: xfrm.y + oy,
      cx: xfrm.cx, cy: xfrm.cy,
      color: fill,
    };
    const prst = getShapePreset(xml);
    if (prst === "ellipse") {
      result.borderRadius = "50%";
    } else if (prst && prst.startsWith("roundRect")) {
      result.borderRadius = String(Math.min(xfrm.cx, xfrm.cy) * 0.15) + "em";
    }
    const alphaM = xml.match(/<a:alpha\s+val="(\d+)"/);
    if (alphaM) result.opacity = parseInt(alphaM[1]) / 100000;
    return result;
  }

  return null;
}

function extractPic(xml, relMap, ox, oy) {
  const xfrm = getXfrm(xml);
  if (!xfrm) return null;
  const blip = xml.match(/<a:blip[^>]*\s+r:embed="([^"]+)"/);
  if (!blip) return null;
  const rId = blip[1];
  const target = relMap[rId];
  if (!target) return null;

  return {
    type: "image",
    x: xfrm.x + ox, y: xfrm.y + oy,
    cx: xfrm.cx, cy: xfrm.cy,
    file: path.basename(target),
  };
}

function extractGraphicFrame(xml, themeFonts, ox, oy) {
  const xfrm = getXfrm(xml);
  if (!xfrm) return null;
  const txBodies = xml.match(/<a:txBody>[\s\S]*?<\/a:txBody>/g);
  if (!txBodies) return null;
  const allPars = [];
  for (const tb of txBodies) allPars.push(...extractTextRuns(tb, themeFonts));
  if (allPars.length === 0) return null;
  return { type: "text", x: xfrm.x + ox, y: xfrm.y + oy, cx: xfrm.cx, cy: xfrm.cy, paragraphs: allPars };
}

/**
 * Collect shapes from XML, preserving PPT spTree element order for correct z-order.
 * Processes sp, pic, grpSp, and graphicFrame elements in document order, recursing into groups.
 * Elements later in the spTree appear later in the array (higher z-index).
 */
function collectShapes(xml, themeFonts, relMap, ox, oy) {
  const shapes = [];
  const elemRegex = /<(p:sp|p:pic|p:grpSp|p:graphicFrame)>[\s\S]*?<\/\1>/g;
  let lastGroupOff = null;
  let lastGroupTextY = null, lastGroupTextCy = null;
  let m;
  while ((m = elemRegex.exec(xml)) !== null) {
    const tag = m[1];
    const content = m[0];
    if (tag === "p:grpSp") {
      const innerMatch = content.match(/<p:grpSp>([\s\S]*?)<\/p:grpSp>/);
      const inner = innerMatch ? innerMatch[1] : "";
      const ch = getGrpChOff(inner);
      const gox = ch ? ch.x - ch.chOffX : 0;
      const goy = ch ? ch.y - ch.chOffY : 0;
      if (ch) {
        lastGroupOff = { x: ch.x, y: ch.y };
        // Find first text child's absolute y within this group
        const spRegex = /<p:sp>[\s\S]*?<\/p:sp>/g;
        let spM;
        while ((spM = spRegex.exec(inner)) !== null) {
          if (spM[0].includes('<p:txBody>')) {
            const tCheck = spM[0].match(/<a:t[^>]*>([^<]*)<\/a:t>/);
            if (tCheck && tCheck[1].trim().length > 0) {
              const tf = getXfrm(spM[0]);
              if (tf) {
                lastGroupTextY = ch.y + (tf.y - ch.chOffY);
                lastGroupTextCy = tf.cy;
              }
              break;
            }
          }
        }
      }
      shapes.push(...collectShapes(inner, themeFonts, relMap, ox + gox, oy + goy));
    } else if (tag === "p:pic") {
      let s = extractPic(content, relMap, ox, oy);
      if (s) {
        // Align icon center with the preceding group's text center
        if (lastGroupOff && s.x === lastGroupOff.x && lastGroupTextY !== null) {
          s.y = lastGroupTextY + (lastGroupTextCy - s.cy) / 2;
        }
        shapes.push(s);
      }
    } else if (tag === "p:sp") {
      const s = extractShape(content, themeFonts, ox, oy);
      if (s) shapes.push(s);
    } else if (tag === "p:graphicFrame") {
      const s = extractGraphicFrame(content, themeFonts, ox, oy);
      if (s) shapes.push(s);
    }
  }
  return shapes;
}

async function convertToHTML(pptxPath, outputDir) {
  const JSZip = require("jszip");

  outputDir = outputDir || path.join(path.dirname(pptxPath), path.basename(pptxPath, ".pptx") + "_html");
  const imgDir = path.join(outputDir, "images");
  fs.mkdirSync(imgDir, { recursive: true });

  const pptxData = fs.readFileSync(pptxPath);
  const zip = await JSZip.loadAsync(pptxData);

  // Extract media
  for (const name of Object.keys(zip.files)) {
    if (name.startsWith("ppt/media/")) {
      const buf = await zip.files[name].async("nodebuffer");
      fs.writeFileSync(path.join(imgDir, path.basename(name)), buf);
    }
  }

  // Load theme fonts
  const themeFonts = await loadThemeFontsAsync(zip);
  console.log("  Theme fonts:", JSON.stringify(themeFonts));

  // Cache layout/master XML
  const layouts = {}, masters = {};
  for (const name of Object.keys(zip.files)) {
    if (name.startsWith("ppt/slideLayouts/") && name.endsWith(".xml")) {
      layouts[name] = normalizeXmlNs(await zip.files[name].async("text"));
    }
    if (name.startsWith("ppt/slideMasters/") && name.endsWith(".xml")) {
      masters[name] = normalizeXmlNs(await zip.files[name].async("text"));
    }
  }

  // Get slide files
  const slideNames = Object.keys(zip.files)
    .filter((n) => /^ppt\/slides\/slide\d+\.xml$/.test(n))
    .sort((a, b) => parseInt(a.match(/slide(\d+)\./)[1]) - parseInt(b.match(/slide(\d+)\./)[1]));

  const slides = [];
  const allUsedFonts = [];
  for (const sName of slideNames) {
    const slideXml = normalizeXmlNs(await zip.files[sName].async("text"));
    const slideNum = sName.match(/slide(\d+)\./)[1];
    const slideBase = "ppt/slides";

    // Relationships for slide
    const slideRels = await getRelsMapAsync(zip, sName);

    // Resolve slide rels targets
    const slideRelResolved = {};
    for (const [rid, target] of Object.entries(slideRels)) {
      const full = resolveRelTarget(rid, slideRels, slideBase);
      if (full) slideRelResolved[rid] = full;
    }

    // Find layout for this slide (via slide rels, not slide XML)
    let layoutXml = null, layoutRels = {}, layoutBase = "";
    const slideLayoutTypes = Object.entries(slideRels).filter(([rid, target]) =>
      target.includes("slideLayout") || Object.values(slideRels).some((v, i, a) =>
        a.includes(target) && target.includes("slideLayout")
      )
    );
    // Find the slideLayout relationship by scanning rels for Type containing "slideLayout"
    for (const [rid, target] of Object.entries(slideRels)) {
      const relsPath = sName.replace(/\.xml$/, ".rels").replace("ppt/slides/", "ppt/slides/_rels/");
      // We need to check the rels XML for the type
      // Since slideRels only has target, check if target contains "slideLayout"
      if (target.includes("slideLayout")) {
        for (const [lName, lXml] of Object.entries(layouts)) {
          if (lName.endsWith("/" + path.basename(target))) {
            layoutXml = lXml;
            layoutBase = lName.replace(/\/[^/]+$/, "");
            layoutRels = await getRelsMapAsync(zip, lName);
            break;
          }
        }
        break;
      }
    }
    // Fallback: if relMap doesn't contain slideLayout, check the rels XML directly
    if (!layoutXml) {
      const relsPath2 = sName.replace(/\.xml$/, ".rels").replace("ppt/slides/", "ppt/slides/_rels/");
      if (zip.files[relsPath2]) {
        const relsXml = await zip.files[relsPath2].async("text");
        const layoutRelMatch = relsXml.match(/<Relationship[^>]*slideLayout[^>]*Target="([^"]+)"/);
        if (layoutRelMatch) {
          const target = layoutRelMatch[1];
          for (const [lName, lXml] of Object.entries(layouts)) {
            if (lName.endsWith("/" + path.basename(target))) {
              layoutXml = lXml;
              layoutBase = lName.replace(/\/[^/]+$/, "");
              layoutRels = await getRelsMapAsync(zip, lName);
              break;
            }
          }
        }
      }
    }

    // Find master for layout (via layout rels for slideMaster)
    let masterXml = null, masterRels = {}, masterBase = "";
    if (layoutXml && Object.keys(layoutRels).length > 0) {
      for (const [rid, target] of Object.entries(layoutRels)) {
        if (target.includes("slideMaster")) {
          for (const [mName, mXml] of Object.entries(masters)) {
            if (mName.endsWith("/" + path.basename(target))) {
              masterXml = mXml;
              masterBase = mName.replace(/\/[^/]+$/, "");
              masterRels = await getRelsMapAsync(zip, mName);
              break;
            }
          }
          break;
        }
      }
    }

    // Resolve layout rel targets
    const layoutRelResolved = {};
    for (const [rid, target] of Object.entries(layoutRels)) {
      const full = resolveRelTarget(rid, layoutRels, layoutBase);
      if (full) layoutRelResolved[rid] = full;
    }

    // Resolve master rel targets
    const masterRelResolved = {};
    for (const [rid, target] of Object.entries(masterRels)) {
      const full = resolveRelTarget(rid, masterRels, masterBase);
      if (full) masterRelResolved[rid] = full;
    }

    // Background color (slide -> layout -> master)
    let bgColor = "#ffffff";
    let bgImage = null;
    const slideBgColor = parseBgColor(slideXml);
    if (slideBgColor) bgColor = slideBgColor;
    const slideBgImg = parseBgImage(slideXml, slideRelResolved);
    if (slideBgImg) bgImage = slideBgImg;

    if (!bgImage && layoutXml) {
      const layoutBgColor = parseBgColor(layoutXml);
      if (!slideBgColor && layoutBgColor) bgColor = layoutBgColor;
      const layoutBgImg = parseBgImage(layoutXml, layoutRelResolved);
      if (layoutBgImg) bgImage = layoutBgImg;
    }

    if (!bgImage && masterXml) {
      const masterBgColor = parseBgColor(masterXml);
      if (!slideBgColor && bgColor === "#ffffff" && masterBgColor) bgColor = masterBgColor;
      const masterBgImg = parseBgImage(masterXml, masterRelResolved);
      if (masterBgImg) bgImage = masterBgImg;
    }

    // Collect shapes from master -> layout -> slide (layered)
    let shapes = [];

    // Master shapes
    if (masterXml) {
      shapes.push(...collectShapes(masterXml, themeFonts, masterRelResolved, 0, 0));
    }

    // Layout shapes
    if (layoutXml) {
      shapes.push(...collectShapes(layoutXml, themeFonts, layoutRelResolved, 0, 0));
    }

    // Slide shapes (top layer)
    const slideShapes = collectShapes(slideXml, themeFonts, slideRelResolved, 0, 0);
    shapes.push(...slideShapes);

    shapes.forEach(sh => {
      if (sh.type === "text" && sh.usedFonts) {
        sh.usedFonts.forEach(fn => { if (!allUsedFonts.includes(fn)) allUsedFonts.push(fn); });
      }
    });
    slides.push({ bgColor, bgImage, shapes });
    const nImg = shapes.filter(s => s.type === "image").length;
    const nTxt = shapes.filter(s => s.type === "text").length;
    const nFill = shapes.filter(s => s.type === "fill").length;
    console.log(`  Slide ${slideNum}: ${nImg} img + ${nTxt} txt + ${nFill} fill`);
  }

  // Ensure theme fonts are included for CDN loading
  for (const key of ["+mj-lt", "+mj-ea", "+mn-lt", "+mn-ea"]) {
    const fn = themeFonts[key];
    if (fn && !allUsedFonts.includes(fn)) allUsedFonts.push(fn);
  }

  const fontCss = genFontFaces(allUsedFonts);
  const hasNormAutofit = slides.some(s => s.shapes.some(sh => sh.type === "text" && sh.normAutofit));

  // Build HTML
  const html = `<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>${path.basename(pptxPath, ".pptx")}</title>
<style>
* { margin: 0; padding: 0; box-sizing: border-box; }
html { font-size: 16px; }
body {
  font-family: "Microsoft YaHei","PingFang SC","Helvetica Neue",sans-serif;
  background: #222; overflow: hidden; height: 100vh;
  display: flex; align-items: center; justify-content: center;
}
.ppt-container {
  position: relative;
  width: min(177.78vh, 100vw); height: min(56.25vw, 100vh);
  overflow: hidden; background: #fff;
  container-type: inline-size;
}
.slide {
  position: absolute; top: 0; left: 0; width: 100%; height: 100%;
  display: none; overflow: hidden;
}
.slide.active { display: block; }
${fontCss}
${hasNormAutofit ? ".norm-autofit{overflow:hidden;display:flex;flex-direction:column;}\n" : ""}
.nav-bar {
  position: fixed; bottom: 20px; left: 50%;
  transform: translateX(-50%);
  display: flex; gap: 12px; align-items: center;
  background: rgba(0,0,0,0.6); padding: 8px 18px;
  border-radius: 20px; color: #fff; font-size: 14px; z-index: 9999;
}
.nav-bar button {
  background: rgba(255,255,255,0.2); color: #fff;
  border: none; border-radius: 50%;
  width: 32px; height: 32px; cursor: pointer;
  font-size: 18px; line-height: 32px; text-align: center;
}
.nav-bar button:hover { background: rgba(255,255,255,0.4); }
</style>
</head>
<body>
<div class="ppt-container">
${slides.map((s, i) => {
  let bgStyle = "background-color:" + s.bgColor + ";";
  if (s.bgImage) bgStyle += "background-image:url(images/" + s.bgImage + ");background-size:cover;background-position:center;";
  return `<div class="slide${i === 0 ? " active" : ""}" style="${bgStyle}">\n${s.shapes.map(sh => {
    const l = emuPct(sh.x, PPT_W);
    const t = emuPct(sh.y, PPT_H);
    const w = emuPct(sh.cx, PPT_W);
    const h = emuPct(sh.cy, PPT_H);
    if (sh.type === "image") {
      return `  <div style="position:absolute;left:${l}%;top:${t}%;width:${w}%;height:${h}%;"><img src="images/${sh.file}" style="display:block;max-width:100%;max-height:100%;margin:auto;position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);"></div>`;
    }
    if (sh.type === "fill") {
      let fStyle = `position:absolute;left:${l}%;top:${t}%;width:${w}%;height:${h}%;background-color:${sh.color};`;
      if (sh.borderRadius) fStyle += `border-radius:${sh.borderRadius};`;
      if (sh.opacity !== undefined && sh.opacity < 1) fStyle += `opacity:${sh.opacity};`;
      return `  <div style="${fStyle}"></div>`;
    }
    if (sh.type === "text") {
      const lIns = sh.lIns || 91440;
      const rIns = sh.rIns || 91440;
      const tIns = sh.tIns || 45720;
      const bIns = sh.bIns || 45720;
      const tClass = sh.normAutofit ? "norm-autofit" : "";
      let style = `position:absolute;left:${l}%;top:${t}%;width:${w}%;height:${h}%;overflow:visible;white-space:pre-wrap;word-wrap:break-word;`;
      style += `padding:${(tIns/121920).toFixed(4)}cqw ${(rIns/121920).toFixed(4)}cqw ${(bIns/121920).toFixed(4)}cqw ${(lIns/121920).toFixed(4)}cqw;`;
      if (sh.bgColor) {
        style += `background-color:${sh.bgColor};`;
        if (sh.bgOpacity !== undefined) style += `opacity:${sh.bgOpacity};`;
      }
      const anchor = sh.anchor || "t";
      if (anchor === "ctr") style += "display:flex;flex-direction:column;justify-content:center;";
      else if (anchor === "b") style += "display:flex;flex-direction:column;justify-content:flex-end;";
      const classAttr = tClass ? ` class="${tClass}"` : "";
      return `  <div${classAttr} style="${style}">${sh.paragraphs.map(p => {
        let pStyle = "text-align:" + ({ l: "left", ctr: "center", r: "right", just: "justify" }[p.align] || "center") + ";";
        if (p.lineSp) pStyle += "line-height:" + p.lineSp + ";";
        return `<p style="${pStyle}">${p.runs.map(r => {
          let rStyle = "";
          if (r.fontSize) rStyle += `font-size:${(r.fontSize / 9.6).toFixed(4)}cqw;`;
          if (r.bold) rStyle += "font-weight:bold;";
          if (r.italic) rStyle += "font-style:italic;";
          if (r.color) rStyle += "color:" + r.color + ";";
          if (r.spc) rStyle += `letter-spacing:${(r.spc/9.6).toFixed(4)}cqw;`;
          const fn = r.latinFont || r.eaFont;
          const cf = cssFont(fn, themeFonts);
          if (cf) rStyle += "font-family:" + cf + ";";
          return `<span style="${rStyle}">${escHtml(r.text)}</span>`;
        }).join("")}</p>`;
      }).join("\n")}</div>`;
    }
    return "";
  }).join("\n")}</div>`;
}).join("\n")}
</div>
<div class="nav-bar">
  <button onclick="changeSlide(-1)">&#9664;</button>
  <span id="pageInfo">1 / ${slides.length}</span>
  <button onclick="changeSlide(1)">&#9654;</button>
</div>
<script>
let current = 0, total = ${slides.length};
function showSlide(idx) {
  document.querySelectorAll('.slide').forEach(s => s.classList.remove('active'));
  document.querySelectorAll('.slide')[idx].classList.add('active');
  document.getElementById('pageInfo').textContent = (idx + 1) + ' / ' + total;
  current = idx;
}
function changeSlide(d) { let n = current + d; if (n < 0) n = total - 1; if (n >= total) n = 0; showSlide(n); }
document.addEventListener('keydown', e => { if (e.key === 'ArrowLeft' || e.key === 'ArrowUp') changeSlide(-1); if (e.key === 'ArrowRight' || e.key === 'ArrowDown') changeSlide(1); });
let touchStartX = 0;
document.addEventListener('touchstart', e => { touchStartX = e.touches[0].clientX; });
document.addEventListener('touchend', e => { const diff = e.changedTouches[0].clientX - touchStartX; if (Math.abs(diff) > 50) changeSlide(diff < 0 ? 1 : -1); });
showSlide(0);
</script>
</body>
</html>`;

  fs.writeFileSync(path.join(outputDir, "index.html"), html, "utf-8");
  return { htmlPath: path.join(outputDir, "index.html"), outputDir };
}

module.exports = { convertToHTML };
