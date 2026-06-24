/* ================================================================
   AgentChat HTML 版 - 认证错误码映射
   对应原版 core/utils/authErrorMessage.ts
   ================================================================ */

/**
 * 将服务端错误映射为用户可读中文文案
 */
export function mapAuthErrorMessage(error, fallback) {
  const raw = error instanceof Error ? error.message : String(error ?? '').trim();
  if (!raw) return fallback;
  const normalized = raw.toLowerCase();

  if (/too\s*many|rate\s*limit|frequen|频繁|过于频繁|太频繁|超限|次数/.test(normalized)) {
    return '请求过于频繁，请稍后再试';
  }
  if (/black|blocked|denied|forbid|refuse|拦截|拒绝|黑名单|风控/.test(normalized)) {
    return '该手机号暂时无法接收验证码，请更换号码或稍后再试';
  }
  if (/empty response|network|timeout|unreach|disconnect|连接|超时|网络/.test(normalized)) {
    return '网络波动或服务繁忙，请稍后重试';
  }
  if (/invalid|phone|mobile|format|号码|手机号/.test(normalized)) {
    return '手机号格式不正确或不支持';
  }
  if (/invalid_or_expired_otp|验证码已过期|验证码错误/.test(normalized)) {
    return '验证码错误或已过期，请重新获取';
  }
  if (/otp_verify_failed|验证失败/.test(normalized)) {
    return '验证码校验失败，请重新获取';
  }
  return raw;
}
