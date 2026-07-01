/**
 * homework.js — End-to-end homework PPT generation workflow
 *
 * Usage:
 *   node index.js homework <url> [模版路径]
 *
 * Steps:
 *   1. Fetch & parse homework HTML
 *   2. Analyze template (a:t count per slide)
 *   3. Download media (images + audio)
 *   4. Generate PPT via XML injection
 *   5. Convert to HTML
 *   6. Post-process HTML (inject slide content + audio manager)
 *   7. Place output in templates/
 */

const path = require("path");
const fs = require("fs");
const https = require("https");
const http = require("http");

const SKILL_DIR = path.resolve(__dirname, "..");
const TEMPLATES_DIR = path.join(SKILL_DIR, "templates");
const TEMPLATE_DIR = path.join(SKILL_DIR, "模版");

// ---------- helpers ----------
function E(cm) { return Math.round(cm * 360000); }
function ID() { return Math.floor(Math.random() * 9000 + 1000); }
function escXml(s) { return String(s).replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;").replace(/"/g,"&quot;"); }

function makeTextSp(text, x, y, w, h, o) {
  o = o || {};
  var sz = o.sz || 1400, clr = o.clr || "333333", b = o.bold ? ' b="1"' : "";
  var al = { l:"l", c:"ctr", r:"r" }[o.align||"l"] || "l";
  var fn = o.font ? ' font="' + o.font + '"' : "";
  return '<p:sp><p:nvSpPr><p:cNvPr id="' + ID() + '" name="T"/><p:cNvSpPr txBox="1"/><p:nvPr/></p:nvSpPr>' +
    '<p:spPr><a:xfrm><a:off x="' + x + '" y="' + y + '"/><a:ext cx="' + w + '" cy="' + h + '"/></a:xfrm>' +
    '<a:prstGeom prst="rect"><a:avLst/></a:prstGeom><a:noFill/></p:spPr>' +
    '<p:txBody><a:bodyPr wrap="square" rtlCol="0" algn="' + al + '"><a:spAutoFit/></a:bodyPr>' +
    '<p:p><p:r><p:rPr lang="zh-CN" sz="' + sz + '"' + b + fn + '>' +
    '<a:solidFill><a:srgbClr val="' + clr + '"/></a:solidFill></p:rPr>' +
    '<p:t>' + escXml(text) + '</p:t></p:r></p:p></p:txBody></p:sp>';
}

function makeImageSp(rid, x, y, w, h) {
  return '<p:sp><p:nvSpPr><p:cNvPr id="' + ID() + '" name="I"/><p:cNvSpPr/><p:nvPr/></p:nvSpPr>' +
    '<p:spPr><a:xfrm><a:off x="' + x + '" y="' + y + '"/><a:ext cx="' + w + '" cy="' + h + '"/></a:xfrm>' +
    '<a:prstGeom prst="rect"><a:avLst/></a:prstGeom><a:noFill/></p:spPr>' +
    '<p:blipFill><a:blip r:embed="' + rid + '"/><a:stretch><a:fillRect/></a:stretch></p:blipFill></p:sp>';
}

function makeRels(entries) {
  var xml = '<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">';
  entries.forEach(function(e) { xml += '<Relationship Id="' + e.id + '" Type="' + e.type + '" Target="' + e.target + '"/>'; });
  return xml + '</Relationships>';
}

// ---------- fetch URL ----------
function fetchUrl(url) {
  return new Promise(function(resolve, reject) {
    var mod = url.startsWith("https") ? https : http;
    mod.get(url, { timeout: 30000 }, function(res) {
      var data = "";
      res.on("data", function(c) { data += c; });
      res.on("end", function() { resolve(data); });
    }).on("error", reject);
  });
}

// ---------- download file ----------
function downloadFile(url, dest) {
  return new Promise(function(resolve, reject) {
    var mod = url.startsWith("https") ? https : http;
    mod.get(url, { timeout: 30000 }, function(res) {
      if (res.statusCode !== 200) { reject(new Error("HTTP " + res.statusCode + " for " + url)); return; }
      var ws = fs.createWriteStream(dest);
      res.pipe(ws);
      ws.on("finish", function() { ws.close(); resolve(dest); });
    }).on("error", reject);
  });
}

// ---------- parse homework HTML ----------
function parseHomework(html, baseUrl) {
  var result = {
    title: "\u8BFE\u540E\u4F5C\u4E1A",
    subtitle: "",
    words: [],
    activities: [],
    images: [],
    audioFiles: [],
    quizData: null,  // { veg: [], animal: [], correctVeg: [], correctAnimal: [] }
    _readingText: ""  // temp storage for reading text during parsing
  };

  var base = baseUrl.replace(/\/[^/]*$/, "/");

  // Title from .header h1
  var titleM = html.match(/<div[^>]*class="header"[^>]*>[\s\S]*?<h1[^>]*>([^<]+)<\/h1>/);
  if (titleM) result.title = titleM[1].replace(/&#?[a-z0-9]+;/g, "").trim();

  // Subtitle from .header p (2nd line)
  var subM = html.match(/<div[^>]*class="header"[^>]*>[\s\S]*?<p>([^<]+)<\/p>/);
  if (subM) result.subtitle = subM[1].trim();

  // Extract ALL audio URLs from JavaScript DATA variables
  var jsAudioRegex = /(?:audio|CHANT_AUDIO|SING_AUDIO|READ_AUDIO|talkAudioUrl|phonicsAudioUrl)\s*[:=]\s*['"]([^'"]+\.mp3)['"]/g;
  var jsAudioMatch;
  var jsAudioUrls = [];
  var namedAudioUrls = {};  // name -> url for matching to sections
  while ((jsAudioMatch = jsAudioRegex.exec(html)) !== null) {
    var url = resolveUrl(jsAudioMatch[1], base);
    // Always record named audio URL mapping even if URL is duplicate
    var varName = jsAudioMatch[0].match(/(\w+)\s*[:=]/);
    if (varName && url) namedAudioUrls[varName[1]] = url;
    // Only push to jsAudioUrls if URL is new (for the audio files list)
    if (url && !jsAudioUrls.some(function(u) { return u === url; })) {
      jsAudioUrls.push(url);
    }
  }
  // Also from onclick playAudio
  var clickRegex = /playAudio\(['"]([^'"]+\.mp3)['"]/g;
  while ((clickMatch = clickRegex.exec(html)) !== null) {
    var url = resolveUrl(clickMatch[1], base);
    if (url && !jsAudioUrls.some(function(u) { return u === url; })) jsAudioUrls.push(url);
  }
  // Also from data-audio or src attributes
  var attrAudioRegex = /(?:data-audio|src)="([^"]+\.mp3)"/g;
  while ((attrMatch = attrAudioRegex.exec(html)) !== null) {
    var url = resolveUrl(attrMatch[1], base);
    if (url && !jsAudioUrls.some(function(u) { return u === url; })) jsAudioUrls.push(url);
  }
  jsAudioUrls.forEach(function(u) {
    result.audioFiles.push({ url: u, name: path.basename(u.replace(/[?#].*$/, "")) });
  });

  // Extract ALL image URLs from JavaScript DATA arrays (READING_DATA images and any other img src)
  var jsImgRegex = /img\s*:\s*['"]([^'"]+\.(?:png|jpg|jpeg))['"]/g;
  var jsImgMatch;
  var jsImgUrls = [];
  while ((jsImgMatch = jsImgRegex.exec(html)) !== null) {
    var url = resolveUrl(jsImgMatch[1], base);
    if (url && !jsImgUrls.some(function(u) { return u === url; })) jsImgUrls.push(url);
  }
  // Also from <img src="...">
  var htmlImgRegex = /<img[^>]*src="([^"]+\.(?:png|jpg|jpeg))"[^>]*>/g;
  while ((htmlImgMatch = htmlImgRegex.exec(html)) !== null) {
    var url = resolveUrl(htmlImgMatch[1], base);
    if (url && !jsImgUrls.some(function(u) { return u === url; })) jsImgUrls.push(url);
  }
  jsImgUrls.forEach(function(u) {
    result.images.push({ url: u, name: path.basename(u.replace(/[?#].*$/, "")) });
  });

  // Extract activity sections from <div class="card" data-section="N"> or <section class="mission-section" data-task="N">
  var cardMap = {};
  // Match div-based cards
  var cardOpenRegex = /<div[^>]*class="[^"]*card[^"]*"[^>]*data-section="(\d+)"[^>]*>/g;
  var cardMatch;
  var cardOpenings = [];
  while ((cardMatch = cardOpenRegex.exec(html)) !== null) {
    cardOpenings.push({ num: parseInt(cardMatch[1]), idx: cardMatch.index, tagLen: cardMatch[0].length, tagType: "div" });
  }
  // Match section-based mission-sections
  var sectionOpenRegex = /<section[^>]*class="[^"]*mission-section[^"]*"[^>]*data-task="(\d+)"[^>]*>/g;
  while ((sectionMatch = sectionOpenRegex.exec(html)) !== null) {
    cardOpenings.push({ num: parseInt(sectionMatch[1]), idx: sectionMatch.index, tagLen: sectionMatch[0].length, tagType: "section" });
  }
  // Sort by position
  cardOpenings.sort(function(a, b) { return a.idx - b.idx; });
  // For each opening, find matching closing tag by counting nested tags
  cardOpenings.forEach(function(o) {
    var start = o.idx + o.tagLen;
    var depth = 1;
    var closeTag = o.tagType === "section" ? "<\\/section>" : "<\\/div>";
    var openTag = o.tagType === "section" ? "<section" : "<div";
    var tagRegex = new RegExp("<\\/" + (o.tagType === "section" ? "section" : "div") + "[\\s>]|<" + (o.tagType === "section" ? "section" : "div") + "[\\s>]", "g");
    tagRegex.lastIndex = start;
    var tm;
    while ((tm = tagRegex.exec(html)) !== null) {
      if (tm[0].indexOf("</") === 0) {
        depth--;
        if (depth === 0) {
          cardMap[o.num] = html.substring(start, tm.index);
          break;
        }
      } else {
        depth++;
      }
    }
  });

  // Extract words from JavaScript arrays (use double-quote only to handle apostrophes)
  var wordRegex = /\{\s*word:\s*"([^"]+)"\s*,\s*zh:\s*"([^"]+)"\s*,\s*audio:\s*"([^"]+\.mp3)"/g;
  while ((wordMatch = wordRegex.exec(html)) !== null) {
    result.words.push({
      word: wordMatch[1],
      zh: wordMatch[2],
      audioUrl: resolveUrl(wordMatch[3], base)
    });
  }
  // Also extract words from HTML word-card elements
  var wordCardRegex = /<div[^>]*class="[^"]*word-card[^"]*"[^>]*data-word="([^"]*)"[^>]*data-audio="([^"]*)"[^>]*>[\s\S]*?<img[^>]*src="([^"]+\.(?:png|jpg|jpeg))"[^>]*>[\s\S]*?<span[^>]*class="[^"]*word-label[^"]*"[^>]*>([\s\S]*?)<\/span>/g;
  var wcMatch;
  while ((wcMatch = wordCardRegex.exec(html)) !== null) {
    var wordText = wcMatch[4].trim();
    // Avoid duplicates
    if (wordText && !result.words.some(function(w) { return w.word === wordText; })) {
      result.words.push({
        word: wordText,
        zh: "",
        audioUrl: resolveUrl(wcMatch[2], base),
        imgUrl: resolveUrl(wcMatch[3], base)
      });
    }
  }

  // Build activities from cards
  for (var sn = 1; sn <= 5; sn++) {
    var secHtml = cardMap[sn];
    if (!secHtml) continue;

    // Title from <h2>
    var h2M = secHtml.match(/<h2[^>]*>([\s\S]*?)<\/h2>/);
    var title = "";
    if (h2M) {
      title = h2M[1].replace(/<[^>]+>/g, "").trim();
      // Remove leading number like "1 " or "1."
      title = title.replace(/^\d+\s*[.\u3001\s]*\s*/, "").trim();
    }
    if (!title) {
      // Try from text after <span class="num">
      var numM = secHtml.match(/<span[^>]*class="num"[^>]*>.*?<\/span>\s*([^<]+)/);
      if (numM) title = numM[1].trim();
    }

    // Description: from task-desc or styled p
    var descM = secHtml.match(/<p[^>]*class="task-desc"[^>]*>([\s\S]*?)<\/p>/) || secHtml.match(/<p[^>]*style="color:#888[^"]*"[^>]*>([^<]+)<\/p>/);
    var desc = descM ? descM[1].replace(/<[^>]+>/g, "").trim() : "";

    // Audio URL: from named JS vars, onclick, data-audio, or src
    var secAudioUrl = "";
    // Match section to named audio variables
    if (sn === 2 && namedAudioUrls["CHANT_AUDIO"]) secAudioUrl = namedAudioUrls["CHANT_AUDIO"];
    else if (sn === 3 && namedAudioUrls["SING_AUDIO"]) secAudioUrl = namedAudioUrls["SING_AUDIO"];
    else if (sn === 4) { secAudioUrl = namedAudioUrls["READ_AUDIO"] || namedAudioUrls["talkAudioUrl"] || ""; }
    else if (sn === 5 && namedAudioUrls["phonicsAudioUrl"]) secAudioUrl = namedAudioUrls["phonicsAudioUrl"];
    if (!secAudioUrl) {
      var secAudioM = secHtml.match(/playAudio\(['"]([^'"]+\.mp3)['"]/);
      if (secAudioM) secAudioUrl = resolveUrl(secAudioM[1], base);
    }
    if (!secAudioUrl) {
      var secAudioData = secHtml.match(/data-audio="([^"]+\.mp3)"/);
      if (secAudioData) secAudioUrl = resolveUrl(secAudioData[1], base);
    }

    // Images from this section's <img> tags
    var secImgRegex = /<img[^>]*src="([^"]+\.(?:png|jpg|jpeg))"[^>]*>/g;
    var secImgs = [];
    while ((secImgM = secImgRegex.exec(secHtml)) !== null) {
      var u = resolveUrl(secImgM[1], base);
      if (u) secImgs.push(u);
    }

    // Also grab images and text from JS data relevant to this section
    // Reading images and text belong to section 4
    if (sn === 4) {
      var readingDataRegex = /READING_DATA\s*=\s*\[([\s\S]*?)\];/;
      var rdM = html.match(readingDataRegex);
      if (rdM) {
        var imgFromJs = rdM[1].match(/img:\s*['"]([^'"]+\.(?:png|jpg|jpeg))['"]/g);
        if (imgFromJs) {
          imgFromJs.forEach(function(im) {
            var url = im.match(/['"]([^'"]+)['"]/);
            if (url) {
              var resolved = resolveUrl(url[1], base);
              if (resolved && !secImgs.some(function(i) { return i === resolved; })) secImgs.push(resolved);
            }
          });
        }
        // Extract reading text from each entry (use double-quote only to handle apostrophes)
        var textFromJs = rdM[1].match(/text:\s*"([^"]+)"/g);
        if (textFromJs) {
          var readingLines = [];
          textFromJs.forEach(function(entry) {
            var t = entry.match(/"([^"]+)"/);
            if (t && t[1]) readingLines.push(t[1]);
          });
          if (readingLines.length > 0) {
            result._readingText = readingLines.join("<br>");
          }
        }
      }
    }

    // Store extra content for this specific section
    var secExtra = "";

    // Chant lines (section 2)
    if (sn === 2) {
      var chantBoxM = secHtml.match(/<div[^>]*class="chant-box"[^>]*>([\s\S]*?)<\/div>\s*$/);
      if (chantBoxM) secExtra = chantBoxM[1];
    }
    // Song lyrics (section 3)
    if (sn === 3) {
      var songM = secHtml.match(/<div[^>]*class="song-lines"[^>]*>([\s\S]*?)<\/div>/);
      if (songM) secExtra = songM[1];
    }
    // Reading text from JS variable extraction above
    if (sn === 4 && result._readingText) secExtra = result._readingText;

    result.activities.push({
      title: title,
      desc: desc,
      audioUrl: secAudioUrl,
      images: secImgs,
      _sectionNum: sn,
      _extraHtml: secExtra
    });
  }

  // Extract quiz data
  var quizVegM = html.match(/var\s+QUIZ_VEG\s*=\s*\[([^\]]+)\];/);
  var quizAnimalM = html.match(/var\s+QUIZ_ANIMAL\s*=\s*\[([^\]]+)\];/);
  var correctVegM = html.match(/var\s+CORRECT_VEG\s*=\s*\[([^\]]+)\];/);
  var correctAnimalM = html.match(/var\s+CORRECT_ANIMAL\s*=\s*\[([^\]]+)\];/);
  if (quizVegM || quizAnimalM) {
    result.quizData = {
      veg: quizVegM ? quizVegM[1].split(",").map(function(s) { return s.trim().replace(/['"]/g,""); }) : [],
      animal: quizAnimalM ? quizAnimalM[1].split(",").map(function(s) { return s.trim().replace(/['"]/g,""); }) : [],
      correctVeg: correctVegM ? correctVegM[1].split(",").map(function(s) { return s.trim().replace(/['"]/g,""); }) : [],
      correctAnimal: correctAnimalM ? correctAnimalM[1].split(",").map(function(s) { return s.trim().replace(/['"]/g,""); }) : []
    };
  }

  return result;
}

function resolveUrl(url, base) {
  if (!url) return "";
  if (url.startsWith("http://") || url.startsWith("https://")) return url;
  if (url.startsWith("//")) return "https:" + url;
  // Remove leading ./ relative
  url = url.replace(/^\.\//, "");
  // Handle paths like ./public/audio/... by removing leading ./
  while (url.indexOf("../") === 0) { url = url.substring(3); base = base.replace(/\/[^/]*\/?$/, "/"); }
  return base.replace(/\/$/, "") + "/" + url.replace(/^\//, "");
}

// ---------- analyze template ----------
async function analyzeTemplate(templatePath) {
  var JSZip = require("jszip");
  var zip = await JSZip.loadAsync(fs.readFileSync(templatePath));
  var slides = [];
  for (var i = 1; ; i++) {
    var sf = zip.files["ppt/slides/slide" + i + ".xml"];
    if (!sf) break;
    var xml = await sf.async("string");
    var atCount = (xml.match(/<a:t[^>]*>/g) || []).length;
    var hasBg = xml.indexOf("<p:bg>") > -1;
    var bgImg = xml.match(/r:embed="([^"]+)"/);
    slides.push({ num: i, atCount: atCount, hasBg: hasBg, bgRid: bgImg ? bgImg[1] : null });
  }
  // Get rels for background images
  var rels = {};
  for (var i = 1; i <= slides.length; i++) {
    var rf = zip.files["ppt/slides/_rels/slide" + i + ".xml.rels"];
    if (rf) {
      var rXml = await rf.async("string");
      var relMatches = rXml.match(/<Relationship\s+Id="([^"]+)"[^>]*?Target="([^"]+)"/g);
      if (relMatches) {
        relMatches.forEach(function(r) {
          var parts = r.match(/Id="([^"]+)"[^>]*Target="([^"]+)"/);
          if (parts) {
            if (!rels[i]) rels[i] = {};
            rels[i][parts[1]] = parts[2];
          }
        });
      }
    }
  }
  return { slideCount: slides.length, slides: slides, rels: rels };
}

// ---------- generate PPT ----------
async function generatePPT(templatePath, parsed, mediaDir, outputPptx) {
  var JSZip = require("jszip");
  var DOMParser = require("@xmldom/xmldom").DOMParser;
  var XMLSerializer = require("@xmldom/xmldom").XMLSerializer;
  var zip = await JSZip.loadAsync(fs.readFileSync(templatePath));
  var template = await analyzeTemplate(templatePath);

  // Collect unique media files
  var imgFiles = [], audioFiles = [];
  parsed.words.forEach(function(w) {
    if (w.imgUrl) { var n = path.basename(w.imgUrl.replace(/[?#].*$/,"")); if (!imgFiles.some(function(f){return f.name===n})) imgFiles.push({name:n,url:w.imgUrl}); }
    if (w.audioUrl) { var n = path.basename(w.audioUrl.replace(/[?#].*$/,"")); if (!audioFiles.some(function(f){return f.name===n})) audioFiles.push({name:n,url:w.audioUrl}); }
  });
  parsed.activities.forEach(function(a) {
    if (a.audioUrl) { var n = path.basename(a.audioUrl.replace(/[?#].*$/,"")); if (!audioFiles.some(function(f){return f.name===n})) audioFiles.push({name:n,url:a.audioUrl}); }
    if (a.images) a.images.forEach(function(u) {
      var n = path.basename(u.replace(/[?#].*$/,""));
      if (!imgFiles.some(function(f){return f.name===n})) imgFiles.push({name:n,url:u});
    });
  });
  parsed.images.forEach(function(img) {
    if (!imgFiles.some(function(f){return f.name===img.name})) imgFiles.push(img);
  });
  // Also add from parsed.audioFiles
  parsed.audioFiles.forEach(function(a) {
    if (!audioFiles.some(function(f){return f.name===a.name})) audioFiles.push(a);
  });

  // Download media
  var dlPromises = [];
  imgFiles.forEach(function(f) {
    var dst = path.join(mediaDir, f.name);
    if (!fs.existsSync(dst)) dlPromises.push(downloadFile(f.url, dst));
  });
  audioFiles.forEach(function(f) {
    var dst = path.join(mediaDir, f.name);
    if (!fs.existsSync(dst)) dlPromises.push(downloadFile(f.url, dst));
  });
  await Promise.all(dlPromises);

  // Plan slide content — one activity per slide, centered layout
  var slideContent = {};
  var relsData = {};
  var totalSlides = template.slideCount;

  // Slide 1: title (modify existing a:t)
  slideContent[1] = { type: "replace", texts: [parsed.title + " / " + parsed.subtitle] };

  // Slides 2-5: one activity per slide (combine chant+sing if more activities than slides)
  var actCount = parsed.activities.length;
  var maxContentSlides = totalSlides - 1;
  if (actCount > maxContentSlides) {
    // Combine idx 1 (Chant) and idx 2 (Sing) into one combined activity
    var chantAct = parsed.activities[1];
    var singAct = parsed.activities[2];
    if (chantAct && singAct) {
      var combined = {
        title: chantAct.title + " & " + singAct.title,
        desc: (chantAct.desc || "") + " / " + (singAct.desc || ""),
        audioUrl: chantAct.audioUrl || singAct.audioUrl,
        images: (chantAct.images || []).concat(singAct.images || []),
        _extraHtml: (chantAct._extraHtml || "") + "\n\n" + (singAct._extraHtml || ""),
        _chantAudio: chantAct.audioUrl,
        _singAudio: singAct.audioUrl,
        _chantDesc: chantAct.desc,
        _singDesc: singAct.desc,
        _combined: true
      };
      // Rebuild activities list: keep index 0, replace 1-2 with combined, keep 3,4
      var newActs = [parsed.activities[0], combined, parsed.activities[3], parsed.activities[4]];
      parsed.activities = newActs;
    }
  }
  parsed.activities.forEach(function(act, idx) {
    var sn = idx + 2; // slide 2,3,4,5
    if (sn > totalSlides) return;
    slideContent[sn] = { type: "activityDetail", activity: act, index: idx + 1, total: parsed.activities.length, activityIdx: idx };
    if (act._combined) slideContent[sn]._combined = true;
  });

  // Build rels and inject XML
  var cntRid = {};
  template.slides.forEach(function(s) { cntRid[s.num] = 4; if (!relsData[s.num]) relsData[s.num] = []; });

  for (var sn in slideContent) {
    sn = parseInt(sn);
    var sc = slideContent[sn];
    var sf = zip.files["ppt/slides/slide" + sn + ".xml"];
    if (!sf) continue;
    var xml = await sf.async("string");

    if (sc.type === "replace" && sc.texts) {
      // Modify existing a:t elements
      var doc = new DOMParser().parseFromString(xml, "text/xml");
      var ats = doc.getElementsByTagName("a:t");
      var textIdx = 0;
      for (var ti = 0; ti < ats.length && textIdx < sc.texts.length; ti++) {
        if (ats[ti].textContent && ats[ti].textContent.trim()) {
          while (ats[ti].firstChild) ats[ti].removeChild(ats[ti].firstChild);
          ats[ti].appendChild(doc.createTextNode(sc.texts[textIdx]));
          textIdx++;
        }
      }
      xml = new XMLSerializer().serializeToString(doc);
      zip.file("ppt/slides/slide" + sn + ".xml", xml);
    } else if (sc.type === "activityDetail") {
      var act = sc.activity;
      var sps = "";
      var contentTop = 1.2;
      var boxW = 30, boxH = 0.7;
      var centerX = (33.87 - boxW) / 2;
      sps += makeTextSp("\u4efb\u52a1 " + sc.index + "/" + sc.total, E(centerX), E(contentTop), E(boxW), E(0.5), { sz:1200, clr:"#999", align:"c" });
      sps += makeTextSp(act.title, E(centerX - 1), E(contentTop + 0.6), E(boxW + 2), E(0.8), { sz:2200, bold:true, clr:"#E74C3C", align:"c" });
      if (act.desc) {
        sps += makeTextSp(act.desc, E(centerX), E(contentTop + 1.5), E(boxW), E(0.6), { sz:1300, clr:"#666", align:"c" });
      }
      // Word cards for first activity - show word images + labels below title
      var wordCardsY = 0;
      if (sc.activityIdx === 0 && parsed.words && parsed.words.length > 0) {
        wordCardsY = contentTop + 2.2;
        var perRow = 4;
        var wcW = 3.5, wcImgH = 2.5, wcH = 3.5, wcGapX = 0.8, wcGapY = 0.5;
        parsed.words.forEach(function(w, wi) {
          var col = wi % perRow, row = Math.floor(wi / perRow);
          var cx = 2 + col * (wcW + wcGapX);
          var cy = wordCardsY + row * (wcH + wcGapY);
          if (w.imgUrl) {
            var imgName = path.basename(w.imgUrl.replace(/[?#].*$/,""));
            var imgRid = "rId" + (cntRid[sn]++);
            sps += makeImageSp(imgRid, E(cx), E(cy), E(wcW), E(wcImgH));
            relsData[sn].push({ id: imgRid, type: "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image", target: "../media/" + imgName });
          }
          sps += makeTextSp(w.word, E(cx), E(cy + (w.imgUrl ? wcImgH + 0.1 : 0)), E(wcW), E(0.6), { sz:1400, bold:true, clr:"#E65100", align:"c" });
          if (w.audioUrl) {
            var aName = path.basename(w.audioUrl.replace(/[?#].*$/,""));
            var rid = "rId" + (cntRid[sn]++);
            relsData[sn].push({ id: rid, type: "http://schemas.openxmlformats.org/officeDocument/2006/relationships/media", target: "../media/" + aName });
          }
        });
      }

      var imgs = act.images || [];
      // Image grid vars (hoisted for audio btn positioning)
      var imgStartY = wordCardsY > 0 ? wordCardsY + 3.5 : contentTop + 2.5;
      var imgGridRows = 0, imgH = 6, imgGapY = 1;
      if (imgs.length > 0) {
        var imgsToShow = imgs.slice(0, 4);
        var cols = imgsToShow.length === 1 ? 1 : 2;
        imgGridRows = Math.ceil(imgsToShow.length / cols);
        var imgW = cols === 1 ? 12 : 8;
        imgH = cols === 1 ? 9 : 6;
        var imgGapX = 1.5, imgGapY = 1;
        var totalW = cols * imgW + (cols - 1) * imgGapX;
        var startX = (33.87 - totalW) / 2;
        imgStartY = contentTop + 2.5;
        imgsToShow.forEach(function(u, i) {
          var col = i % cols, row = Math.floor(i / cols);
          var x = startX + col * (imgW + imgGapX);
          var y = imgStartY + row * (imgH + imgGapY);
          var imgName = path.basename(u.replace(/[?#].*$/,""));
          var rid = "rId" + (cntRid[sn]++);
          sps += makeImageSp(rid, E(x), E(y), E(imgW), E(imgH));
          relsData[sn].push({ id: rid, type: "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image", target: "../media/" + imgName });
        });
      }
      var btnY = imgs.length > 0 ? (imgStartY + (imgGridRows * imgH + (imgGridRows - 1) * imgGapY) + 0.5) : (contentTop + 3.5);
      if (act._combined) {
        // Combined slide: show two audio buttons
        if (act._chantAudio) {
          var aName = path.basename(act._chantAudio.replace(/[?#].*$/,""));
          sps += makeTextSp("\u25B6 \u8D5E", E(centerX + 1), E(btnY), E(boxW / 2 - 2), E(0.6), { sz:1200, bold:true, clr:"#66BB6A", align:"c" });
          var rid1 = "rId" + (cntRid[sn]++);
          relsData[sn].push({ id: rid1, type: "http://schemas.openxmlformats.org/officeDocument/2006/relationships/media", target: "../media/" + aName });
        }
        if (act._singAudio) {
          var sName = path.basename(act._singAudio.replace(/[?#].*$/,""));
          sps += makeTextSp("\u25B6 \u6B4C", E(centerX + boxW / 2 + 3), E(btnY), E(boxW / 2 - 2), E(0.6), { sz:1200, bold:true, clr:"#AB47BC", align:"c" });
          var rid2 = "rId" + (cntRid[sn]++);
          relsData[sn].push({ id: rid2, type: "http://schemas.openxmlformats.org/officeDocument/2006/relationships/media", target: "../media/" + sName });
        }
      } else if (act.audioUrl) {
        var aName = path.basename(act.audioUrl.replace(/[?#].*$/,""));
        var audioRid = "rId" + (cntRid[sn]++);
        sps += makeTextSp("\u25B6 \u64AD\u653E\u97F3\u9891", E(centerX + 4), E(btnY), E(boxW - 8), E(0.6), { sz:1400, bold:true, clr:"#80CBC4", align:"c" });
        relsData[sn].push({ id: audioRid, type: "http://schemas.openxmlformats.org/officeDocument/2006/relationships/media", target: "../media/" + aName });
      }
      // Quiz section: show tags
      if (sc.activityIdx === 3 && parsed.quizData) {
        var qd = parsed.quizData;
        var qzY = btnY + 1.2;
        if (qd.veg && qd.veg.length > 0) {
          sps += makeTextSp("\u852C\u83DC: " + qd.veg.join(" / ") + (qd.correctVeg ? "  \u2713=" + qd.correctVeg.join(",") : ""), E(centerX), E(qzY), E(boxW), E(0.5), { sz:1100, clr:"#43A047", align:"c" });
        }
        if (qd.animal && qd.animal.length > 0) {
          sps += makeTextSp("\u52A8\u7269: " + qd.animal.join(" / ") + (qd.correctAnimal ? "  \u2713=" + qd.correctAnimal.join(",") : ""), E(centerX), E(qzY + 0.7), E(boxW), E(0.5), { sz:1100, clr:"#1E88E5", align:"c" });
        }
      }
      xml = xml.replace("</p:spTree>", sps + "</p:spTree>");
      zip.file("ppt/slides/slide" + sn + ".xml", xml);
    }
  }

  // Write rels files
  for (var sn2 in relsData) {
    if (relsData[sn2].length > 0) {
      // Read original rels XML and keep as-is; just append new entries
      var origRelsPath = "ppt/slides/_rels/slide" + sn2 + ".xml.rels";
      var origRelsXml = "";
      if (zip.files[origRelsPath]) {
        origRelsXml = await zip.files[origRelsPath].async("string");
        // Remove closing tag
        origRelsXml = origRelsXml.replace('</Relationships>', '');
      } else {
        origRelsXml = '<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">';
      }
      // Append new entries
      var mediaType = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
      relsData[sn2].forEach(function(e) {
        origRelsXml += '<Relationship Id="' + e.id + '" Type="' + e.type + '" Target="' + e.target + '"/>';
      });
      origRelsXml += '</Relationships>';
      zip.file(origRelsPath, origRelsXml);
    }
  }

  // Add media files to zip
  imgFiles.forEach(function(f) {
    var p = path.join(mediaDir, f.name);
    if (fs.existsSync(p)) zip.file("ppt/media/" + f.name, fs.readFileSync(p));
  });
  audioFiles.forEach(function(f) {
    var p = path.join(mediaDir, f.name);
    if (fs.existsSync(p)) zip.file("ppt/media/" + f.name, fs.readFileSync(p));
  });

  // Generate
  var buf = await zip.generateAsync({ type: "nodebuffer", compression: "DEFLATE" });
  fs.writeFileSync(outputPptx, buf);
  return outputPptx;
}

// ---------- post-process HTML (optimized layout) ----------
function postProcessHTML(tempDir, parsed) {
  var htmlPath = path.join(tempDir, "index.html");
  if (!fs.existsSync(htmlPath)) return;
  var html = fs.readFileSync(htmlPath, "utf-8");

  // Build per-slide content — one activity per slide
  var slideContents = {};
  var cleanTitle = parsed.title.replace(/&#?[a-z0-9]+;/g,"").trim();

  parsed.activities.forEach(function(act, idx) {
    var sn = idx + 2; // slide 2,3,4,5
    var num = idx + 1, total = parsed.activities.length;
    // Auto-fit: font sizes use cqw (container-width relative), overflow:hidden, no px caps.
    var html = '<div style="width:88%;max-width:88cqw;margin:0 auto;text-align:center;display:flex;flex-direction:column;align-items:center;justify-content:center;max-height:100%;overflow:hidden;gap:0.8cqw">';
    // Activity index
    html += '<div style="font-size:2cqw;color:#999;flex-shrink:0">\u4efb\u52a1 ' + num + '/' + total + '</div>';
    // Title
    html += '<div style="font-size:3.5cqw;font-weight:bold;color:#E74C3C;flex-shrink:0;max-width:100%;overflow:hidden;text-overflow:ellipsis;white-space:nowrap">' + escHtml(act.title) + '</div>';
    // Description
    if (act.desc) {
      html += '<div style="font-size:2.2cqw;color:#666;flex-shrink:0;max-width:100%;overflow:hidden;text-overflow:ellipsis;white-space:nowrap">' + escHtml(act.desc) + '</div>';
    }

    // Word cards for the first activity
    if (idx === 0 && parsed.words && parsed.words.length > 0) {
      html += '<div style="display:flex;flex-wrap:wrap;justify-content:center;gap:0.8cqw;flex-shrink:1;overflow:hidden;max-width:100%;padding:0.3cqw">';
      parsed.words.forEach(function(w) {
        var aName = w.audioUrl ? path.basename(w.audioUrl.replace(/[?#].*$/,"")) : "";
        var imgName = w.imgUrl ? path.basename(w.imgUrl.replace(/[?#].*$/,"")) : "";
        html += '<div style="background:#FFF3E0;border-radius:1cqw;padding:0.5cqw 1.5cqw;text-align:center;border:1px solid #FFCC80;flex:0 0 auto;' +
          (aName ? 'cursor:pointer' : '') + '"' +
          (aName ? ' data-audio="' + aName + '"' : '') + '>' +
          (imgName ? '<img src="images/' + imgName + '" style="width:6cqw;height:6cqw;object-fit:contain;display:block;margin:0 auto 0.2cqw;border-radius:0.4cqw">' : '') +
          '<div style="font-size:2.5cqw;font-weight:bold;color:#E65100;max-width:100%;overflow:hidden;text-overflow:ellipsis">' + escHtml(w.word) + '</div>' +
          (w.zh ? '<div style="font-size:1.5cqw;color:#8D6E63">' + escHtml(w.zh) + '</div>' : '') +
          '</div>';
      });
      html += '</div>';
    }

    // Extra text content for chant / song / reading (from act._extraHtml)
    var extra = act._extraHtml || "";
    if (extra) {
      extra = extra.replace(/<script[\s\S]*?<\/script>/g, "");
      html += '<div style="font-size:1.8cqw;color:#555;line-height:1.5;flex-shrink:1;overflow:hidden;max-height:45%;margin:0.3cqw 0;padding:0.4cqw 0.8cqw;background:rgba(255,255,255,0.5);border-radius:0.6cqw">';
      extra = extra.replace(/<br\s*\/?>/gi, "\n");
      extra = extra.replace(/<[^>]+>/g, "");
      var lines = extra.split("\n").map(function(l) { return l.trim(); }).filter(function(l) { return l; });
      if (lines.length > 0) {
        lines.forEach(function(l) {
          html += '<div style="padding:0.15cqw 0">' + escHtml(l) + '</div>';
        });
      }
      html += '</div>';
    }

    // Images (centered grid, no overflow) — skip if word cards already show images
    var hasWordImgs = (idx === 0 && parsed.words && parsed.words.some(function(w) { return w.imgUrl; }));
    if (act.images && act.images.length > 0 && !hasWordImgs) {
      var imgCount = Math.min(act.images.length, 4);
      var imgWidthPct = imgCount === 1 ? '55%' : imgCount === 2 ? '42%' : imgCount === 3 ? '30%' : '22%';
      html += '<div style="display:flex;flex-wrap:wrap;justify-content:center;align-items:center;gap:0.8cqw;flex-shrink:1;overflow:hidden;max-width:100%;padding:0 0.5cqw">';
      act.images.slice(0, 4).forEach(function(u) {
        var imgName = path.basename(u.replace(/[?#].*$/,""));
        html += '<img src="images/' + imgName + '" style="width:' + imgWidthPct + ';max-width:' + imgWidthPct + ';height:auto;max-height:30cqw;object-fit:contain;border-radius:0.8cqw;border:1px solid #ddd;box-shadow:0 0.1cqw 0.2cqw rgba(0,0,0,0.08);flex-shrink:1">';
      });
      html += '</div>';
    }

    // Quiz section: show tags for section 5
    if (idx === 3 && parsed.quizData) {
      var qd = parsed.quizData;
      var quizHtml = '<div style="flex-shrink:1;overflow:hidden;max-height:45%;margin:0.3cqw 0;padding:0.4cqw 0.8cqw;background:rgba(255,255,255,0.5);border-radius:0.6cqw;text-align:center">';
      if (qd.veg && qd.veg.length > 0) {
        quizHtml += '<div style="font-weight:bold;font-size:1.8cqw;color:#666;margin:0.3cqw 0">Vegetables</div>';
        quizHtml += '<div style="display:flex;flex-wrap:wrap;justify-content:center;gap:0.6cqw;margin-bottom:0.4cqw">';
        qd.veg.forEach(function(v) {
          var correct = qd.correctVeg && qd.correctVeg.indexOf(v) > -1;
          quizHtml += '<span style="padding:0.3cqw 1cqw;border-radius:1.2cqw;font-size:1.6cqw;font-weight:bold;border:2px solid ' + (correct ? '#81C784' : '#E0E0E0') + ';background:' + (correct ? '#C8E6C9' : '#fff') + ';color:' + (correct ? '#2E7D32' : '#555') + '">' + escHtml(v) + (correct ? ' ✓' : '') + '</span>';
        });
        quizHtml += '</div>';
      }
      if (qd.animal && qd.animal.length > 0) {
        quizHtml += '<div style="font-weight:bold;font-size:1.8cqw;color:#666;margin:0.3cqw 0">Animals</div>';
        quizHtml += '<div style="display:flex;flex-wrap:wrap;justify-content:center;gap:0.6cqw;margin-bottom:0.4cqw">';
        qd.animal.forEach(function(a) {
          var correct = qd.correctAnimal && qd.correctAnimal.indexOf(a) > -1;
          quizHtml += '<span style="padding:0.3cqw 1cqw;border-radius:1.2cqw;font-size:1.6cqw;font-weight:bold;border:2px solid ' + (correct ? '#64B5F6' : '#E0E0E0') + ';background:' + (correct ? '#BBDEFB' : '#fff') + ';color:' + (correct ? '#1565C0' : '#555') + '">' + escHtml(a) + (correct ? ' ✓' : '') + '</span>';
        });
        quizHtml += '</div>';
      }
      quizHtml += '</div>';
      html += quizHtml;
    }
    // Audio buttons
    if (act._combined) {
      if (act._chantAudio) {
        var aName = path.basename(act._chantAudio.replace(/[?#].*$/,""));
        html += '<div style="flex-shrink:0;margin-top:0.3cqw"><button style="background:#66BB6A;display:inline-block;padding:0.4cqw 2.2cqw;border:none;border-radius:1.8cqw;font-size:1.8cqw;font-weight:bold;cursor:pointer;color:#fff;max-width:100%;overflow:hidden" data-audio="' + aName + '">\u25B6 \u64AD\u653E\u97F3\u9891(\u97F5\u6587)</button></div>';
      }
      if (act._singAudio) {
        var sName = path.basename(act._singAudio.replace(/[?#].*$/,""));
        html += '<div style="flex-shrink:0;margin-top:0.3cqw"><button style="background:#AB47BC;display:inline-block;padding:0.4cqw 2.2cqw;border:none;border-radius:1.8cqw;font-size:1.8cqw;font-weight:bold;cursor:pointer;color:#fff;max-width:100%;overflow:hidden" data-audio="' + sName + '">\u25B6 \u64AD\u653E\u97F3\u9891(\u6B4C\u66F2)</button></div>';
      }
    } else {
      var aName = act.audioUrl ? path.basename(act.audioUrl.replace(/[?#].*$/,"")) : null;
      if (aName) {
        html += '<div style="flex-shrink:0;margin-top:0.3cqw"><button style="background:#80CBC4;display:inline-block;padding:0.5cqw 2.5cqw;border:none;border-radius:2cqw;font-size:2.2cqw;font-weight:bold;cursor:pointer;color:#fff;max-width:100%;overflow:hidden" data-audio="' + aName + '">\u25B6 \u64AD\u653E\u97F3\u9891</button></div>';
      }
    }
    html += '</div>';
    slideContents[sn] = html;
  });

  // Inject into slides by order, fix white overlays and add flex centering
  var headEnd = html.indexOf('<div class="slide');
  if (headEnd === -1) { fs.writeFileSync(htmlPath, html, "utf-8"); return; }
  var beforeSlides = html.substring(0, headEnd);
  var slidesHtml = html.substring(headEnd);

  var result = beforeSlides;
  var searchStart = 0;
  for (var si = 1; si <= 5; si++) {
    var slideTagStart = slidesHtml.indexOf('<div class="slide', searchStart);
    if (slideTagStart === -1) break;
    var gtPos = slidesHtml.indexOf(">", slideTagStart);
    if (gtPos === -1) break;
    var nextSlide = slidesHtml.indexOf('<div class="slide', gtPos + 1);
    var slideContentEnd = nextSlide > -1 ? nextSlide : slidesHtml.length;

    var openTag = slidesHtml.substring(slideTagStart, gtPos + 1);
    var inner = slidesHtml.substring(gtPos + 1, slideContentEnd);

    // Extract white overlay fill divs (from <p:bg> elements in the template)
    var fillDivs = "";
    inner = inner.replace(
      /<div style="position:absolute;left:0\.0000%;top:0\.0000%;width:100\.0000%;height:100\.0000%;background-color:#ffffff;opacity:[^"]+;"><\/div>/g,
      function(m) {
        fillDivs = m.replace('opacity:', 'pointer-events:none;z-index:1;opacity:');
        return "";
      }
    );

    var content = slideContents[si] || "";
    if (content) {
      // Wrap in a full-size flex container for centering (does NOT touch slide's own display)
      var flexContainer = '<div style="position:absolute;top:0;left:0;width:100%;height:100%;display:flex;align-items:center;justify-content:center;flex-direction:column;z-index:2">';
      content = flexContainer + content + '</div>';
    }

    var newSlideContent = openTag + "\n" + fillDivs + "\n" + content + "\n" + inner;
    result += newSlideContent;
    searchStart = slideContentEnd;
    if (nextSlide === -1) break;
  }
  if (searchStart < slidesHtml.length) result += slidesHtml.substring(searchStart);
  html = result;

  // Add audio manager
  var audioScript = '<style>.pa{animation:pp 1s ease-in-out infinite}@keyframes pp{0%,100%{box-shadow:0 0 0 0 rgba(128,203,196,0.5)}50%{box-shadow:0 0 0 1cqw rgba(128,203,196,0)}}</style>' +
    '<script>!function(){var a=null,b=null;document.querySelectorAll("[data-audio]").forEach(function(e){e.addEventListener("click",function(){var t="images/"+this.getAttribute("data-audio");if(b===this&&a&&!a.paused){a.pause();x(0);return}if(a){a.pause();a.currentTime=0}a=new Audio(t);b=this;x(1);a.play();a.addEventListener("ended",function(){x(0);b=null;a=null})})});function x(p){if(!b)return;if(p){b.classList.add("pa");b.innerHTML="\u23F8 \u6682\u505C"}else{b.removeAttribute("class");b.innerHTML="\u25B6 "+b.textContent.replace(/[\u25B6\u23F8]/g,"").trim()}}}();</script>';
  html = html.replace("</body>", audioScript + "\n</body>");

  fs.writeFileSync(htmlPath, html, "utf-8");
}

function escHtml(s) { return String(s).replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;").replace(/"/g,"&quot;"); }

// ---------- main entry ----------
async function generateHomeworkPPT(homeworkUrl, templatePath, outputDir) {
  var templateName = templatePath || path.join(TEMPLATE_DIR, "\u4F5C\u4E1A\u6A21\u72481.pptx");
  if (!fs.existsSync(templateName)) {
    console.error("\u6A21\u677F\u6587\u4EF6\u4E0D\u5B58\u5728: " + templateName);
    process.exit(1);
  }

  var uuid = require("crypto").randomUUID ? require("crypto").randomUUID() : Date.now() + "-" + Math.random().toString(36).slice(2);
  var workDir = path.join(outputDir || TEMPLATES_DIR, "temp-" + uuid);
  var mediaDir = path.join(workDir, "media");
  fs.mkdirSync(mediaDir, { recursive: true });

  console.log("  \u6B63\u5728\u83B7\u53D6\u4F5C\u4E1A\u9875\u9762...");
  var html = await fetchUrl(homeworkUrl);

  console.log("  \u6B63\u5728\u89E3\u6790\u4F5C\u4E1A\u5185\u5BB9...");
  var parsed = parseHomework(html, homeworkUrl);
  console.log("  \u5355\u8BCD: " + parsed.words.length + ", \u6D3B\u52A8: " + parsed.activities.length + ", \u56FE\u7247: " + parsed.images.length + ", \u97F3\u9891: " + parsed.audioFiles.length);

  console.log("  \u6B63\u5728\u751F\u6210PPT...");
  var pptxPath = path.join(workDir, "homework-review.pptx");
  await generatePPT(templateName, parsed, mediaDir, pptxPath);

  console.log("  \u6B63\u5728\u8F6C\u6362\u4E3AHTML...");
  var { convertToHTML } = require("./convert");
  var result = await convertToHTML(pptxPath, workDir);

  // Move media files to images/ and fix CSS paths
  var imgDir = path.join(workDir, "images");
  if (!fs.existsSync(imgDir)) fs.mkdirSync(imgDir);
  var mediaFiles = fs.readdirSync(mediaDir);
  mediaFiles.forEach(function(f) {
    try { fs.renameSync(path.join(mediaDir, f), path.join(imgDir, f)); } catch(e) {}
  });
  // Also move any extracted media from convert
  try {
    fs.readdirSync(workDir).forEach(function(f) {
      if (/\.(png|jpg|jpeg|mp3)$/i.test(f)) {
        try { fs.renameSync(path.join(workDir, f), path.join(imgDir, f)); } catch(e) {}
      }
    });
  } catch(e) {}
  // Fix CSS url() paths
  var idxHtml = path.join(workDir, "index.html");
  if (fs.existsSync(idxHtml)) {
    var h = fs.readFileSync(idxHtml, "utf-8");
    h = h.replace(/url\(image(\d+\.(?:jpg|jpeg|png))\)/g, 'url(images/image$1)');
    fs.writeFileSync(idxHtml, h, "utf-8");
  }

  console.log("  \u6B63\u5728\u6CE8\u5165\u5185\u5BB9...");
  postProcessHTML(workDir, parsed);

  // Summary
  var pptxSize = (fs.statSync(pptxPath).size / 1024 / 1024).toFixed(1);
  console.log("  \u5B8C\u6210\uFF01");
  console.log("  PPT: " + pptxPath + " (" + pptxSize + " MB)");
  console.log("  HTML: " + path.join(workDir, "index.html"));

  return { pptxPath: pptxPath, htmlPath: path.join(workDir, "index.html"), workDir: workDir };
}

module.exports = { generateHomeworkPPT };
