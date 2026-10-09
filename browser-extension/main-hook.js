(() => {
  const NativeWebSocket = window.WebSocket;
  const sdkSockets = new Set();
  let hoveredCanvas = null;
  const canvasAt = target => {
    const canvas = document.querySelector("canvas#canvas");
    return canvas && target === canvas ? canvas : null;
  };
  const is3Dx = value => {
    try {
      const url = new URL(String(value), location.href);
      return url.hostname === "127.51.68.120" && (url.port === "8181" || url.port === "8182");
    } catch { return false; }
  };
  const announce = () => window.postMessage({
    source: "trackpad-cad-3dx-hook",
    active: document.visibilityState === "visible" && document.hasFocus() &&
      hoveredCanvas !== null && hoveredCanvas === document.querySelector("canvas#canvas"),
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
  const updateHover = target => {
    const canvas = canvasAt(target);
    if (canvas === hoveredCanvas) return;
    hoveredCanvas = canvas;
    announce();
  };
  document.addEventListener("pointerover", event => updateHover(event.target), true);
  document.addEventListener("pointermove", event => updateHover(event.target), true);
  document.addEventListener("pointerout", event => updateHover(event.relatedTarget), true);
  document.addEventListener("pointerleave", () => updateHover(null));
  document.addEventListener("visibilitychange", () => {
    if (document.visibilityState !== "visible") hoveredCanvas = null;
    announce();
  });
  window.addEventListener("focus", () => {
    const canvas = document.querySelector("canvas#canvas");
    hoveredCanvas = canvas?.matches(":hover") ? canvas : null;
    announce();
  });
  window.addEventListener("blur", () => {
    hoveredCanvas = null;
    announce();
  });
  announce();
})();
