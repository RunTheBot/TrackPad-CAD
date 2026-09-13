(() => {
  const NativeWebSocket = window.WebSocket;
  const sdkSockets = new Set();
  const is3Dx = value => {
    try {
      const url = new URL(String(value), location.href);
      return url.hostname === "127.51.68.120" && (url.port === "8181" || url.port === "8182");
    } catch { return false; }
  };
  const announce = () => window.postMessage({
    source: "trackpad-cad-3dx-hook",
    active: document.visibilityState === "visible" && document.hasFocus(),
    sdk: sdkSockets.size > 0,
    url: location.href
  }, "*");
  function HookedWebSocket(url, protocols) {
    const socket = protocols === undefined ? new NativeWebSocket(url) : new NativeWebSocket(url, protocols);
    if (is3Dx(url)) {
      socket.addEventListener("open", () => { sdkSockets.add(socket); announce(); });
      const close = () => { sdkSockets.delete(socket); announce(); };
      socket.addEventListener("close", close);
      socket.addEventListener("error", close);
    }
    return socket;
  }
  Object.setPrototypeOf(HookedWebSocket, NativeWebSocket);
  HookedWebSocket.prototype = NativeWebSocket.prototype;
  for (const key of ["CONNECTING", "OPEN", "CLOSING", "CLOSED"])
    Object.defineProperty(HookedWebSocket, key, { value: NativeWebSocket[key] });
  window.WebSocket = HookedWebSocket;
  document.addEventListener("visibilitychange", announce);
  window.addEventListener("focus", announce);
  window.addEventListener("blur", announce);
  announce();
})();
