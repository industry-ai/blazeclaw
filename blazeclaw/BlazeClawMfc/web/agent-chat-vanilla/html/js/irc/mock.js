/* ================================================================
   IRC 聊天室 - 静态假数据（Mock）
   ----------------------------------------------------------------
   为 chatroom.js 提供展示用的频道、成员、消息等模拟数据。
   所有数据可变，页面操作直接修改本地数据后触发重渲染。
   ================================================================ */

export const MOCK_NICK = "林晓";
export const MOCK_IS_GLOBAL_OP = true;

export const mockChannels = [
  {
    name: "#tech-talk",
    title: "技术交流",
    topic: "讨论技术问题、分享开发经验",
    operators: ["林晓"],
    members: [
      { nick: "林晓", mode: { operator: true }, joinOrder: 1 },
      { nick: "王强", mode: { operator: false }, joinOrder: 2 },
      { nick: "赵敏", mode: { operator: false }, joinOrder: 3 },
      { nick: "陈杰", mode: { operator: false }, joinOrder: 4 },
    ],
  },
  {
    name: "#project-alpha",
    title: "项目Alpha",
    topic: "Alpha 项目协作频道",
    operators: ["林晓", "王强"],
    members: [
      { nick: "林晓", mode: { operator: true }, joinOrder: 1 },
      { nick: "王强", mode: { operator: true }, joinOrder: 2 },
      { nick: "刘洋", mode: { operator: false }, joinOrder: 3 },
      { nick: "孙莉", mode: { operator: false }, joinOrder: 4 },
      { nick: "周明", mode: { operator: false }, joinOrder: 5 },
    ],
  },
  {
    name: "#random",
    title: "闲聊灌水",
    topic: "工作之余的闲聊地带",
    operators: ["赵敏"],
    members: [
      { nick: "赵敏", mode: { operator: true }, joinOrder: 1 },
      { nick: "林晓", mode: { operator: false }, joinOrder: 2 },
      { nick: "陈杰", mode: { operator: false }, joinOrder: 3 },
      { nick: "周明", mode: { operator: false }, joinOrder: 4 },
      { nick: "刘洋", mode: { operator: false }, joinOrder: 5 },
      { nick: "孙莉", mode: { operator: false }, joinOrder: 6 },
    ],
  },
];

export const mockMessages = {
  "#tech-talk": [
    { id: "m1", from: "王强", text: "大家好，最近在研究 WebSocket 的性能优化，有什么好方案吗？", ts: Date.now() - 7200000 },
    { id: "m2", from: "赵敏", text: "可以考虑用 SSE 替代，对于单向推送场景性能更好", ts: Date.now() - 6800000 },
    { id: "m3", from: "林晓", text: "我们项目里用了 WebSocket 连接池，吞吐量提升了大概 40%", ts: Date.now() - 6400000 },
    { id: "m4", from: "陈杰", text: "连接池方案能详细说说吗？我们也遇到类似瓶颈", ts: Date.now() - 6000000 },
    { id: "m5", from: "林晓", text: "核心思路是复用连接、减少握手开销，我可以整理一份文档分享", ts: Date.now() - 5600000 },
    { id: "m6", from: "王强", text: "太好了，期待分享！", ts: Date.now() - 5200000 },
    { id: "m7", from: "赵敏", text: "补充一点，如果用 Nginx 做反代记得调 proxy_read_timeout", ts: Date.now() - 4800000 },
  ],
  "#project-alpha": [
    { id: "m1", from: "王强", text: "Alpha 项目 v1.2 版本今天发布，大家辛苦了", ts: Date.now() - 10800000 },
    { id: "m2", from: "刘洋", text: "终于上线了！测试通过率 98.5%", ts: Date.now() - 10400000 },
    { id: "m3", from: "孙莉", text: "设计稿已经更新到 Figma，大家同步一下最新版本", ts: Date.now() - 10000000 },
    { id: "m4", from: "林晓", text: "后端接口已经部署到预发环境，可以联调了", ts: Date.now() - 9600000 },
    { id: "m5", from: "周明", text: "前端构建包体积优化了 200KB，gzip 后减少 70KB", ts: Date.now() - 9200000 },
    { id: "m6", from: "王强", text: "下个迭代计划周三对齐，各位准备一下各自模块的进度", ts: Date.now() - 8800000 },
  ],
  "#random": [
    { id: "m1", from: "陈杰", text: "今天的咖啡不错，推荐大家试试楼下的新品", ts: Date.now() - 14400000 },
    { id: "m2", from: "赵敏", text: "收到了，下午茶安排上", ts: Date.now() - 14000000 },
    { id: "m3", from: "周明", text: "有人看了昨晚的比赛吗？绝杀太精彩了", ts: Date.now() - 13600000 },
    { id: "m4", from: "刘洋", text: "看了！最后三秒三分球绝杀，心脏受不了", ts: Date.now() - 13200000 },
    { id: "m5", from: "孙莉", text: "周末有人去爬山吗？组织一下", ts: Date.now() - 12800000 },
    { id: "m6", from: "林晓", text: "报名！带上我", ts: Date.now() - 12400000 },
  ],
};

// ── 本地状态 ──
const _listeners = new Set();
let _activeChannel = null;

export function getActiveChannel() {
  return _activeChannel;
}

export function setActiveChannel(name) {
  _activeChannel = name || null;
}

export function subscribe(fn) {
  _listeners.add(fn);
  return () => _listeners.delete(fn);
}

export function notify() {
  for (const fn of _listeners) {
    try { fn(); } catch (e) { console.warn("[chatroom] listener error", e); }
  }
}
