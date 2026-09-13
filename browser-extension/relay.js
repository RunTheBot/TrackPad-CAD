(() => {
  let timer = 0;
  let state = null;
  const pulse = () => state && fetch("http://127.0.0.1:17831/page?sdk="+(state.sdk?"1":"0")+"&url="+encodeURIComponent(state.url), { method: "POST", mode: "cors", cache: "no-store" }).catch(() => {});
  window.addEventListener("message", event => {
    if (event.source !== window || event.data?.source !== "trackpad-cad-3dx-hook") return;
    state = event.data;
    if (state.active) {
      if (!timer) { pulse(); timer = setInterval(pulse, 250); }
    } else if (timer) {
      clearInterval(timer); timer = 0;
      state = null;
    }
  });
})();
