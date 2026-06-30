import AppConfig from '../config.js';

function _toBool(value) {
  return String(value).toLowerCase() === 'true' || value === true;
}

async function _preflightHttpBridgeHealth(baseUrl, signal) {
  const url = `${baseUrl}/health`;
  const resp = await fetch(url, {
    method: 'GET',
    headers: {
      Accept: 'application/json',
    },
    signal,
  });
  if (!resp.ok) {
    throw new Error(`bridge_health_http_${resp.status}`);
  }
  const data = await resp.json().catch(() => ({}));
  if (data && data.ok === false) {
    throw new Error('bridge_health_unhealthy');
  }
  return true;
}

function _supportsNativeWebViewBridge() {
  const cfg = AppConfig.getChatConfig();
  const enabled = String(cfg.enableNativeAgentBridge).toLowerCase() === 'true' || cfg.enableNativeAgentBridge === true;
  return enabled
    && typeof window !== 'undefined'
    && !!window.chrome
    && !!window.chrome.webview
    && typeof window.chrome.webview.postMessage === 'function';
}

function _buildBridgeRequestId() {
  return `agent-bridge-${Date.now()}-${Math.random().toString(16).slice(2)}`;
}

function _traceBridge(requestId, stage, detail) {
  const suffix = detail ? ` ${detail}` : '';
  console.info(`[AgentBridge][${requestId}] ${stage}${suffix}`);
}

function _toError(errorLike, fallbackMessage) {
  if (errorLike instanceof Error) return errorLike;
  const message = typeof errorLike === 'string'
    ? errorLike
    : (errorLike && errorLike.message) || fallbackMessage;
  return new Error(message || fallbackMessage || 'Unknown bridge error');
}

function _parseSseResponse(resp, callbacks) {
  return new Promise(async (resolve, reject) => {
    try {
      const { onDelta, onDone, onToolResult } = callbacks || {};
      const reader = resp.body.getReader();
      const decoder = new TextDecoder();
      let fullText = '';
      let buffer = '';

      while (true) {
        const { done, value } = await reader.read();
        if (done) break;

        buffer += decoder.decode(value, { stream: true });
        const lines = buffer.split('\n');
        buffer = lines.pop() || '';

        for (const line of lines) {
          const trimmed = line.trim();
          if (!trimmed.startsWith('data: ')) continue;

          const jsonStr = trimmed.slice(6);
          if (!jsonStr) continue;

          try {
            const event = JSON.parse(jsonStr);
            if (event.type === 'delta' && event.text) {
              fullText += event.text;
              onDelta?.(event.text, fullText);
            } else if (event.type === 'tool_result') {
              const toolResultContent = String(event.content || '').trim();
              if (toolResultContent) {
                onToolResult?.(toolResultContent);
              }
            } else if (event.type === 'final') {
              const finalText = event.text || fullText;
              onDone?.(finalText);
              resolve({ text: finalText });
              return;
            } else if (event.type === 'error') {
              reject(new Error(event.message || 'Agent stream error'));
              return;
            }
          } catch (parseErr) {
            if (parseErr instanceof SyntaxError) continue;
            reject(parseErr);
            return;
          }
        }
      }

      onDone?.(fullText || '');
      resolve({ text: fullText || '' });
    } catch (err) {
      reject(err);
    }
  });
}

async function _callAgentViaHttpBridge(baseUrl, body, callbacks, signal) {
  const { onDelta, onDone } = callbacks || {};
  const url = `${baseUrl}/api/blazeclaw-agent`;
  const resp = await fetch(url, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
    },
    body: JSON.stringify(body),
    signal,
  });

  if (!resp.ok) {
    const errText = await resp.text().catch(() => '');
    throw new Error(errText || `Agent API error: ${resp.status}`);
  }

  const contentType = resp.headers.get('content-type') || '';
  if (contentType.includes('text/event-stream')) {
    return _parseSseResponse(resp, callbacks);
  }

  const data = await resp.json();
  if (data.ok && data.text) {
    onDelta?.(data.text, data.text);
    onDone?.(data.text);
    return { text: data.text };
  }
  if (data.error) {
    throw new Error(data.error);
  }
  onDone?.('');
  return { text: '' };
}

function _callAgentViaNativeBridge(body, callbacks, signal, timeoutMs) {
  return new Promise((resolve, reject) => {
    const requestId = _buildBridgeRequestId();
    const { onDelta, onDone } = callbacks || {};
    let fullText = '';
    let settled = false;

    const cleanup = () => {
      window.removeEventListener('agentchat.bridge.message', onBridgeMessage);
      if (signal) signal.removeEventListener('abort', onAbortSignal);
      if (timeoutHandle) clearTimeout(timeoutHandle);
    };

    const settleResolve = (result) => {
      if (settled) return;
      settled = true;
      cleanup();
      _traceBridge(requestId, 'native.settle.resolve');
      resolve(result);
    };

    const settleReject = (errorLike) => {
      if (settled) return;
      settled = true;
      cleanup();
      _traceBridge(requestId, 'native.settle.reject', _toError(errorLike, 'Native bridge request failed').message);
      reject(_toError(errorLike, 'Native bridge request failed'));
    };

    const sendAbort = () => {
      try {
        window.chrome.webview.postMessage({
          channel: 'agentchat.bridge.request',
          requestId,
          kind: 'agent.abort',
          payload: {},
        });
      } catch {
      }
    };

    const onAbortSignal = () => {
      sendAbort();
      const abortError = new Error('Agent request aborted');
      abortError.name = 'AbortError';
      _traceBridge(requestId, 'native.abort.signal');
      settleReject(abortError);
    };

    const onBridgeMessage = (event) => {
      const msg = event?.detail;
      if (!msg || typeof msg !== 'object') return;
      if (String(msg.requestId || '') !== requestId) return;

      const channel = String(msg.channel || '');
      if (channel === 'agentchat.bridge.stream.delta') {
        const payload = msg.payload || {};
        const deltaText = String(payload.text || '');
        if (deltaText) {
          _traceBridge(requestId, 'native.stream.delta', `len=${deltaText.length}`);
          fullText += deltaText;
          onDelta?.(deltaText, fullText);
        }
        return;
      }
      if (channel === 'agentchat.bridge.stream.final') {
        const payload = msg.payload || {};
        const finalText = String(payload.text || fullText || '');
        _traceBridge(requestId, 'native.stream.final', `len=${finalText.length}`);
        onDone?.(finalText);
        settleResolve({ text: finalText });
        return;
      }
      if (channel === 'agentchat.bridge.stream.error') {
        const payload = msg.payload || {};
        _traceBridge(requestId, 'native.stream.error', String(payload.message || 'Native bridge stream error'));
        settleReject(new Error(String(payload.message || 'Native bridge stream error')));
        return;
      }
      if (channel === 'agentchat.bridge.response') {
        if (msg.ok === false) {
          const errorMessage = msg?.error?.message || 'Native bridge response error';
          _traceBridge(requestId, 'native.response.error', String(errorMessage));
          settleReject(new Error(String(errorMessage)));
          return;
        }
        if (body.stream !== true) {
          const payload = msg.payload || {};
          const text = String(payload.text || fullText || '');
          _traceBridge(requestId, 'native.response.final', `len=${text.length}`);
          onDone?.(text);
          settleResolve({ text });
        }
      }
    };

    const timeoutHandle = timeoutMs > 0
      ? setTimeout(() => {
          sendAbort();
          _traceBridge(requestId, 'native.timeout', `ms=${timeoutMs}`);
          settleReject(new Error('Agent request timed out'));
        }, timeoutMs)
      : null;

    window.addEventListener('agentchat.bridge.message', onBridgeMessage);
    if (signal) {
      if (signal.aborted) {
        onAbortSignal();
        return;
      }
      signal.addEventListener('abort', onAbortSignal, { once: true });
    }

    try {
      _traceBridge(requestId, 'native.request.posted', 'kind=agent.turn');
      window.chrome.webview.postMessage({
        channel: 'agentchat.bridge.request',
        requestId,
        kind: 'agent.turn',
        payload: body,
      });
    } catch (err) {
      settleReject(err);
    }
  });
}

function _preflightNativeBridgeHealth(signal, timeoutMs = 5000) {
  return new Promise((resolve, reject) => {
    const requestId = _buildBridgeRequestId();
    let settled = false;

    const cleanup = () => {
      window.removeEventListener('agentchat.bridge.message', onBridgeMessage);
      if (signal) signal.removeEventListener('abort', onAbortSignal);
      if (timeoutHandle) clearTimeout(timeoutHandle);
    };

    const settleResolve = (value) => {
      if (settled) return;
      settled = true;
      cleanup();
      resolve(value);
    };

    const settleReject = (errorLike) => {
      if (settled) return;
      settled = true;
      cleanup();
      reject(_toError(errorLike, 'Native bridge health failed'));
    };

    const onAbortSignal = () => {
      const abortError = new Error('Native bridge health aborted');
      abortError.name = 'AbortError';
      _traceBridge(requestId, 'native.health.abort');
      settleReject(abortError);
    };

    const onBridgeMessage = (event) => {
      const msg = event?.detail;
      if (!msg || typeof msg !== 'object') return;
      if (String(msg.requestId || '') !== requestId) return;
      if (String(msg.channel || '') !== 'agentchat.bridge.response') return;

      if (msg.ok === false) {
        const message = msg?.error?.message || 'native_health_response_error';
        _traceBridge(requestId, 'native.health.fail', String(message));
        settleReject(new Error(String(message)));
        return;
      }

      _traceBridge(requestId, 'native.health.ok');
      settleResolve(true);
    };

    const timeoutHandle = timeoutMs > 0
      ? setTimeout(() => {
          _traceBridge(requestId, 'native.health.timeout', `ms=${timeoutMs}`);
          settleReject(new Error('native_health_timeout'));
        }, timeoutMs)
      : null;

    window.addEventListener('agentchat.bridge.message', onBridgeMessage);
    if (signal) {
      if (signal.aborted) {
        onAbortSignal();
        return;
      }
      signal.addEventListener('abort', onAbortSignal, { once: true });
    }

    try {
      _traceBridge(requestId, 'native.health.posted');
      window.chrome.webview.postMessage({
        channel: 'agentchat.bridge.request',
        requestId,
        kind: 'agent.health',
        payload: {},
      });
    } catch (err) {
      settleReject(err);
    }
  });
}

export async function callAgentWithDualTransport({
  baseUrl,
  body,
  callbacks,
  signal,
  timeoutMs,
}) {
  const cfg = AppConfig.getChatConfig();
  const preferNative = String(cfg.agentBridgeTransport || '').toLowerCase() === 'native-webview';
  const nativeBridgeHostStarted = _toBool(cfg.nativeBridgeHostStarted);
  const nativeHttpListenerStarted = _toBool(cfg.nativeHttpListenerStarted);
  const nativeModeDegraded = _toBool(cfg.nativeModeDegraded);
  const reachabilityHint = String(cfg.agentBridgeReachabilityHint || '').trim().toLowerCase();

  if (preferNative && _supportsNativeWebViewBridge()) {
    const allowHttpFallback = String(cfg.enableHttpFallbackOnNativeBridgeError).toLowerCase() !== 'false';
    try {
      await _preflightNativeBridgeHealth(signal, Math.min(Math.max(Math.floor(timeoutMs / 4), 1500), 8000));
      return await _callAgentViaNativeBridge(body, callbacks, signal, timeoutMs);
    } catch (nativeError) {
      const reason = String(nativeError?.message || 'native_bridge_failed');
      const fallbackAllowedByPolicy =
        allowHttpFallback &&
        nativeHttpListenerStarted &&
        !nativeBridgeHostStarted &&
        (reachabilityHint === 'bridge-unavailable' || reachabilityHint === 'http-bridge');

      if (!fallbackAllowedByPolicy) {
        throw new Error(`Native bridge request failed (${reason})`);
      }

      _traceBridge('fallback', 'native->http', `reason=${reason}`);
    }
  }

  const shouldPreflightHttp =
    !preferNative ||
    !nativeBridgeHostStarted ||
    nativeModeDegraded ||
    reachabilityHint === 'bridge-unavailable';

  if (shouldPreflightHttp) {
    try {
      await _preflightHttpBridgeHealth(baseUrl, signal);
    } catch (healthErr) {
      const reason = healthErr instanceof Error ? healthErr.message : 'bridge_health_check_failed';
      throw new Error(`Agent bridge unavailable (${reason})`);
    }
  }

  return _callAgentViaHttpBridge(baseUrl, body, callbacks, signal);
}
