const payload = "base64";
function runRisk() {
  eval("console.log(payload)");
}
new WebSocket("ws://example.com:3333");
