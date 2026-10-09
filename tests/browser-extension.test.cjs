const assert = require("node:assert/strict");
const { readFileSync } = require("node:fs");
const { join } = require("node:path");
const { test } = require("node:test");
const { runInNewContext } = require("node:vm");

function setup() {
  const listeners = () => ({
    events: new Map(),
    addEventListener(type, listener) {
      if (!this.events.has(type)) this.events.set(type, []);
      this.events.get(type).push(listener);
    },
    emit(type, event = {}) {
      for (const listener of this.events.get(type) || []) listener(event);
    }
  });
  class WebSocket {
    constructor() { Object.assign(this, listeners()); }
  }
  Object.assign(WebSocket, { CONNECTING: 0, OPEN: 1, CLOSING: 2, CLOSED: 3 });
  let canvas = null;
  let focused = true;
  const document = Object.assign(listeners(), {
    visibilityState: "visible",
    hasFocus: () => focused,
    querySelector: selector => selector === "canvas#canvas" ? canvas : null
  });
  const requests = [];
  const messages = [];
  const intervals = new Map();
  let nextTimer = 1;
  const window = Object.assign(listeners(), {
    WebSocket,
    postMessage(data) {
      messages.push(data);
      window.emit("message", { source: window, data });
    }
  });
  const context = {
    window, document, URL, location: { href: "https://cad.onshape.com/documents/test" },
    encodeURIComponent,
    fetch: url => { requests.push(new URL(url)); return Promise.resolve(); },
    setInterval: callback => { const id = nextTimer++; intervals.set(id, callback); return id; },
    clearInterval: id => intervals.delete(id)
  };
  for (const file of ["relay.js", "main-hook.js"])
    runInNewContext(readFileSync(join(__dirname, "../browser-extension", file), "utf8"), context);
  return {
    window, document, requests, messages, intervals,
    setCanvas() { canvas = { matches: () => true }; return canvas; },
    setFocus(value) { focused = value; window.emit(value ? "focus" : "blur"); },
    connect(url = "ws://127.51.68.120:8181") {
      const socket = new window.WebSocket(url);
      socket.emit("open");
      return socket;
    },
    active: () => messages.at(-1).active,
    sdk: () => requests.at(-1)?.searchParams.get("sdk")
  };
}

test("only the Onshape canvas activates, including when created after script startup", () => {
  const app = setup();
  app.connect();
  assert.equal(app.active(), false);
  assert.equal(app.requests.length, 0);
  app.document.emit("pointerover", { target: {} });
  assert.equal(app.active(), false);
  const canvas = app.setCanvas();
  app.document.emit("pointerover", { target: canvas });
  assert.equal(app.active(), true);
  assert.equal(app.sdk(), "1");
  assert.equal(app.intervals.size, 1);
  app.document.emit("pointerout", { target: canvas, relatedTarget: {} });
  assert.equal(app.active(), false);
  assert.equal(app.sdk(), "0");
  assert.equal(app.intervals.size, 0);
});

test("an open 3Dconnexion connection is required and disconnects clear the bridge immediately", () => {
  const app = setup();
  const canvas = app.setCanvas();
  app.document.emit("pointermove", { target: canvas });
  assert.equal(app.sdk(), "0");
  app.connect("wss://cad.onshape.com/unrelated");
  assert.equal(app.sdk(), "0");
  const first = app.connect();
  const second = app.connect("ws://127.51.68.120:8182");
  assert.equal(app.sdk(), "1");
  first.emit("close");
  assert.equal(app.sdk(), "1");
  second.emit("error");
  assert.equal(app.sdk(), "0");
});

test("leaving the page, losing focus, and hiding the tab deactivate the bridge", () => {
  const app = setup();
  const canvas = app.setCanvas();
  app.connect();
  app.document.emit("pointerover", { target: canvas });
  app.document.emit("pointerout", { target: canvas, relatedTarget: null });
  assert.equal(app.sdk(), "0");
  app.document.emit("pointermove", { target: canvas });
  app.setFocus(false);
  assert.equal(app.sdk(), "0");
  assert.equal(app.intervals.size, 0);
  app.setFocus(true);
  assert.equal(app.sdk(), "1");
  app.document.visibilityState = "hidden";
  app.document.emit("visibilitychange");
  assert.equal(app.sdk(), "0");
  app.document.visibilityState = "visible";
  app.document.emit("visibilitychange");
  assert.equal(app.active(), false);
  app.document.emit("pointermove", { target: canvas });
  assert.equal(app.sdk(), "1");
  app.document.emit("pointerleave");
  assert.equal(app.sdk(), "0");
});

test("replaced canvases are detected and messages from other windows are ignored", () => {
  const app = setup();
  app.connect();
  const previous = app.setCanvas();
  app.document.emit("pointerover", { target: previous });
  const current = app.setCanvas();
  app.document.emit("pointermove", { target: previous });
  assert.equal(app.sdk(), "0");
  app.document.emit("pointermove", { target: current });
  assert.equal(app.sdk(), "1");
  const count = app.requests.length;
  app.window.emit("message", { source: {}, data: {
    source: "trackpad-cad-3dx-hook", active: false, sdk: false, url: "https://example.com"
  } });
  assert.equal(app.requests.length, count);
  assert.equal(app.intervals.size, 1);
  for (const callback of app.intervals.values()) callback();
  assert.equal(app.sdk(), "1");
});
