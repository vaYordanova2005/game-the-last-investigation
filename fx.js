/* ═══════════════════════════════════════════
   PYTHIA — fx.js
   Motion engine. Zero dependencies.
   · Site-wide WebGL oracle smoke (raw GLSL, no libs)
   · Kinetic typography with cursor repulsion
   · 3D tilt cards, magnetic buttons, page wipes
   Everything degrades: no WebGL → CSS fog,
   reduced motion → static layout.
   ═══════════════════════════════════════════ */
(() => {
  "use strict";

  const q = (sel, root = document) => root.querySelector(sel);
  const qq = (sel, root = document) => [...root.querySelectorAll(sel)];
  const clamp = (v, a = 0, b = 1) => Math.min(b, Math.max(a, v));
  const lerp = (a, b, t) => a + (b - a) * t;

  const reduced = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  const finePointer = window.matchMedia("(pointer: fine)").matches;

  // ───────────────────────────────────────────────────────────────────────────
  // 1 · WEBGL BACKDROP — domain-warped fbm smoke behind the whole site
  // ───────────────────────────────────────────────────────────────────────────
  const VERT = `
    attribute vec2 a_pos;
    void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }
  `;

  const FRAG = `
    precision highp float;
    uniform vec2  u_res;
    uniform float u_time;
    uniform vec2  u_mouse;
    uniform float u_fade;

    float hash(vec2 p) {
      return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
    }
    float noise(vec2 p) {
      vec2 i = floor(p), f = fract(p);
      f = f * f * (3.0 - 2.0 * f);
      float a = hash(i);
      float b = hash(i + vec2(1.0, 0.0));
      float c = hash(i + vec2(0.0, 1.0));
      float d = hash(i + vec2(1.0, 1.0));
      return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
    }
    float fbm(vec2 p) {
      float v = 0.0, a = 0.5;
      mat2 m = mat2(1.6, 1.2, -1.2, 1.6);
      for (int i = 0; i < 6; i++) {
        v += a * noise(p);
        p = m * p;
        a *= 0.5;
      }
      return v;
    }

    void main() {
      vec2 p = (gl_FragCoord.xy - 0.5 * u_res) / u_res.y;
      float t = u_time * 0.055;
      vec2 mo = (u_mouse - 0.5) * vec2(1.4, 0.9);

      // domain warp — two passes, the classic smoke recipe
      vec2 qv = vec2(fbm(p * 1.5 + vec2(0.0, t)),
                     fbm(p * 1.5 + vec2(5.2, 1.3) - t));
      vec2 r  = vec2(fbm(p * 1.5 + 3.6 * qv + vec2(1.7, 9.2) + t * 1.35 + mo * 0.35),
                     fbm(p * 1.5 + 3.6 * qv + vec2(8.3, 2.8) - t * 1.10 - mo * 0.35));
      float f = fbm(p * 1.6 + 3.4 * r);

      vec3 deep = vec3(0.024, 0.052, 0.040);
      vec3 moss = vec3(0.070, 0.215, 0.148);
      vec3 jade = vec3(0.200, 0.520, 0.360);
      vec3 gold = vec3(0.830, 0.660, 0.330);

      vec3 col = mix(deep, moss, smoothstep(0.20, 0.85, f));
      col = mix(col, jade, smoothstep(0.58, 1.05, f) * 0.55);

      // gold veins riding the warp field
      float veins = pow(clamp(length(r) / 1.45, 0.0, 1.0), 3.0);
      col += gold * veins * 0.95;

      // the oracle's breath — a glow that follows the cursor
      float g = exp(-length(p - mo * 1.15) * 2.1);
      col += gold * g * 0.30 + jade * g * 0.16;

      // embers: sparse bright motes drifting upward
      vec2 sp = p * 9.0 + vec2(0.0, -u_time * 0.35);
      float cell = hash(floor(sp));
      float spark = smoothstep(0.988, 1.0, cell) *
                    (0.5 + 0.5 * sin(u_time * 3.0 + cell * 60.0));
      col += gold * spark * 0.55;

      // vignette + film grain
      col *= 1.0 - 0.72 * pow(length(p * vec2(0.72, 1.0)), 2.2);
      col += (hash(gl_FragCoord.xy + fract(u_time)) - 0.5) * 0.032;

      gl_FragColor = vec4(col * u_fade, 1.0);
    }
  `;

  function initBackdrop() {
    const canvas = q("[data-bg-canvas]");
    const root = document.documentElement;
    if (!canvas || reduced) {
      root.classList.add("no-gl");
      return null;
    }

    const gl =
      canvas.getContext("webgl", { antialias: false, alpha: false, powerPreference: "high-performance" }) ||
      canvas.getContext("experimental-webgl");
    if (!gl) {
      root.classList.add("no-gl");
      return null;
    }

    const compile = (type, src) => {
      const sh = gl.createShader(type);
      gl.shaderSource(sh, src);
      gl.compileShader(sh);
      if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) {
        console.warn("[fx] shader:", gl.getShaderInfoLog(sh));
        return null;
      }
      return sh;
    };

    const vs = compile(gl.VERTEX_SHADER, VERT);
    const fs = compile(gl.FRAGMENT_SHADER, FRAG);
    if (!vs || !fs) {
      root.classList.add("no-gl");
      return null;
    }

    const prog = gl.createProgram();
    gl.attachShader(prog, vs);
    gl.attachShader(prog, fs);
    gl.linkProgram(prog);
    if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) {
      root.classList.add("no-gl");
      return null;
    }
    gl.useProgram(prog);

    const buf = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, buf);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);
    const loc = gl.getAttribLocation(prog, "a_pos");
    gl.enableVertexAttribArray(loc);
    gl.vertexAttribPointer(loc, 2, gl.FLOAT, false, 0, 0);

    const uRes = gl.getUniformLocation(prog, "u_res");
    const uTime = gl.getUniformLocation(prog, "u_time");
    const uMouse = gl.getUniformLocation(prog, "u_mouse");
    const uFade = gl.getUniformLocation(prog, "u_fade");

    const mouse = { x: 0.5, y: 0.5, tx: 0.5, ty: 0.5 };

    // the backdrop covers the viewport, so it is sized to the viewport
    function resize() {
      const dpr = Math.min(window.devicePixelRatio || 1, 1.6);
      // innerWidth can still be 0 very early in a background tab; falling back
      // keeps the canvas from being stranded at 0x0 if frames are throttled
      const vw = window.innerWidth || root.clientWidth;
      const vh = window.innerHeight || root.clientHeight;
      if (!vw || !vh) return;
      const w = Math.round(vw * dpr);
      const h = Math.round(vh * dpr);
      if (canvas.width !== w || canvas.height !== h) {
        canvas.width = w;
        canvas.height = h;
        gl.viewport(0, 0, w, h);
      }
    }

    // the smoke reacts to the cursor anywhere on the site
    window.addEventListener("pointermove", (e) => {
      mouse.tx = e.clientX / window.innerWidth;
      mouse.ty = 1 - e.clientY / window.innerHeight;
    }, { passive: true });

    window.addEventListener("resize", resize, { passive: true });
    window.addEventListener("load", resize);
    resize();

    const start = performance.now();
    function frame(now) {
      requestAnimationFrame(frame);
      if (document.hidden) return;
      resize();
      mouse.x = lerp(mouse.x, mouse.tx, 0.06);
      mouse.y = lerp(mouse.y, mouse.ty, 0.06);
      gl.uniform2f(uRes, canvas.width, canvas.height);
      gl.uniform1f(uTime, (now - start) / 1000);
      gl.uniform2f(uMouse, mouse.x, mouse.y);
      gl.uniform1f(uFade, 1);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
    }
    requestAnimationFrame(frame);

    canvas.addEventListener("webglcontextlost", (e) => {
      e.preventDefault();
      root.classList.add("no-gl");
    });
  }

  initBackdrop();

  // ───────────────────────────────────────────────────────────────────────────
  // 2 · KINETIC TITLE — split into chars, staggered reveal, cursor repulsion
  // ───────────────────────────────────────────────────────────────────────────
  function initSplitTitle() {
    const mark = q("[data-split]");
    if (mark) {
      const text = mark.dataset.split || mark.textContent.trim();
      mark.textContent = "";
      const chars = [...text].map((ch, i) => {
        const span = document.createElement("span");
        span.className = "char";
        span.textContent = ch;
        span.style.setProperty("--d", `${0.2 + i * 0.06}s`);
        mark.appendChild(span);
        return span;
      });
      requestAnimationFrame(() => mark.classList.add("in"));
      if (!reduced && finePointer) repel(mark, chars, 120, 0.3);
    }

  }

  // letters shy away from the cursor
  function repel(host, items, radius, strength) {
    let rects = [];
    const measure = () => { rects = items.map((el) => el.getBoundingClientRect()); };
    measure();
    window.addEventListener("resize", measure, { passive: true });
    window.addEventListener("scroll", measure, { passive: true });
    setTimeout(measure, 600);

    const clear = () => items.forEach((el) => {
      el.style.setProperty("--tx", "0px");
      el.style.setProperty("--ty", "0px");
      el.style.setProperty("--rz", "0deg");
    });

    host.addEventListener("pointermove", (e) => {
      items.forEach((el, i) => {
        const r = rects[i];
        if (!r) return;
        const dx = e.clientX - (r.left + r.width / 2);
        const dy = e.clientY - (r.top + r.height / 2);
        const dist = Math.hypot(dx, dy);
        if (dist > radius) {
          el.style.setProperty("--tx", "0px");
          el.style.setProperty("--ty", "0px");
          el.style.setProperty("--rz", "0deg");
          return;
        }
        const force = (1 - dist / radius) ** 2;
        el.style.setProperty("--tx", `${-dx * force * strength}px`);
        el.style.setProperty("--ty", `${-dy * force * strength}px`);
        el.style.setProperty("--rz", `${-dx * force * 0.05}deg`);
      });
    });
    host.addEventListener("pointerleave", clear);
  }
  initSplitTitle();

  // ───────────────────────────────────────────────────────────────────────────
  // 3 · CUSTOM CURSOR — dot snaps, ring trails
  // ───────────────────────────────────────────────────────────────────────────
  function initCursor() {
    if (!finePointer || reduced) return;
    const dot = q("[data-cursor-dot]");
    const ring = q("[data-cursor-ring]");
    if (!dot || !ring) return;

    let mx = innerWidth / 2, my = innerHeight / 2;
    let rx = mx, ry = my;

    addEventListener("pointermove", (e) => {
      mx = e.clientX;
      my = e.clientY;
      dot.style.transform = `translate3d(${mx}px, ${my}px, 0)`;
      const hot = e.target.closest("button, a, input, select, textarea, .book-card, [data-magnetic]");
      document.body.classList.toggle("cursor-hot", !!hot);
    }, { passive: true });

    addEventListener("pointerdown", () => document.body.classList.add("cursor-down"));
    addEventListener("pointerup", () => document.body.classList.remove("cursor-down"));

    (function trail() {
      rx = lerp(rx, mx, 0.16);
      ry = lerp(ry, my, 0.16);
      ring.style.transform = `translate3d(${rx}px, ${ry}px, 0)`;
      requestAnimationFrame(trail);
    })();
  }
  initCursor();

  // ───────────────────────────────────────────────────────────────────────────
  // 4 · MAGNETIC BUTTONS
  // ───────────────────────────────────────────────────────────────────────────
  function initMagnetic() {
    if (!finePointer || reduced) return;
    qq("[data-magnetic]").forEach((el) => {
      el.addEventListener("pointermove", (e) => {
        const r = el.getBoundingClientRect();
        const dx = e.clientX - (r.left + r.width / 2);
        const dy = e.clientY - (r.top + r.height / 2);
        el.style.transform = `translate3d(${dx * 0.28}px, ${dy * 0.4}px, 0)`;
      });
      el.addEventListener("pointerleave", () => { el.style.transform = ""; });
    });
  }
  initMagnetic();

  // ───────────────────────────────────────────────────────────────────────────
  // 4b · TOPBAR — measure it, condense it on scroll, wake the logo up
  // ───────────────────────────────────────────────────────────────────────────
  function initTopbar() {
    const topbar = q("[data-topbar]");
    if (!topbar) return;

    // The bar is fixed, so its height has to become real layout space.
    //
    // Two variables, and the difference matters:
    //   --topbar-h   the EXPANDED height. It is what body padding reserves, so
    //                it must never change when the bar condenses — otherwise the
    //                document shrinks mid-scroll and the page lurches under the
    //                cursor. Measured only while the bar is at full height.
    //   --topbar-now the live height, for things that must sit right below the
    //                bar (the sticky genre rail).
    // The reserved gap only ever exists above the fold, so it is never visible.
    const measure = () => {
      const now = topbar.offsetHeight;
      document.documentElement.style.setProperty("--topbar-now", `${now}px`);
      if (!topbar.classList.contains("stuck")) {
        document.documentElement.style.setProperty("--topbar-h", `${now}px`);
      }
    };
    measure();
    window.addEventListener("resize", measure, { passive: true });
    window.addEventListener("load", measure);
    document.fonts?.ready.then(measure);
    if ("ResizeObserver" in window) new ResizeObserver(measure).observe(topbar);

    // It condenses once you scroll away from the top. The two thresholds are
    // hysteresis: a single one sitting in the middle would flicker.
    const sync = () => {
      const stuck = topbar.classList.contains("stuck");
      if (!stuck && window.scrollY > 70) topbar.classList.add("stuck");
      else if (stuck && window.scrollY < 30) topbar.classList.remove("stuck");
      else return;
      // the height just changed — republish it so the sticky genre rail keeps
      // hugging the bar instead of waiting on the ResizeObserver
      measure();
    };
    sync();
    window.addEventListener("scroll", sync, { passive: true });


    // cart badge reacts when app.js rewrites the count
    const badge = q("[data-cart-count]");
    if (badge && !reduced) {
      let previous = badge.textContent;
      new MutationObserver(() => {
        if (badge.textContent === previous) return;
        previous = badge.textContent;
        badge.classList.remove("bump");
        void badge.offsetWidth;
        badge.classList.add("bump");
      }).observe(badge, { childList: true, characterData: true, subtree: true });
    }
  }
  initTopbar();

  // ───────────────────────────────────────────────────────────────────────────
  // 4c · TYPEWRITER PLACEHOLDER — kinetic type in the search field
  // ───────────────────────────────────────────────────────────────────────────
  function initTypewriter() {
    const input = q("[data-search]");
    if (!input || reduced) return;

    const phrases = [
      "Search by title, author, genre...",
      'Try "Dune"...',
      'Try "Le Guin"...',
      'Try "Horror"...',
      "Ask the oracle...",
    ];
    let phrase = 0, char = 0, deleting = false, paused = false;

    input.addEventListener("focus", () => { paused = true; input.placeholder = phrases[0]; });
    input.addEventListener("blur", () => { paused = false; });

    (function type() {
      if (paused || input.value) {
        setTimeout(type, 900);
        return;
      }
      const target = phrases[phrase];
      char += deleting ? -1 : 1;
      input.placeholder = target.slice(0, char);

      let delay = deleting ? 34 : 68;
      if (!deleting && char === target.length) { deleting = true; delay = 2100; }
      else if (deleting && char === 0) { deleting = false; phrase = (phrase + 1) % phrases.length; delay = 350; }
      setTimeout(type, delay);
    })();
  }
  initTypewriter();

  // ───────────────────────────────────────────────────────────────────────────
  // 5 · REVEAL ON SCROLL — works for markup that arrives later too
  //     fx.css only hides content once <html> carries .fx, so a broken or
  //     blocked script can never leave the catalogue invisible.
  // ───────────────────────────────────────────────────────────────────────────
  if ("IntersectionObserver" in window) document.documentElement.classList.add("fx");

  const revealIO = new IntersectionObserver(
    (entries) => {
      entries.forEach((entry) => {
        if (!entry.isIntersecting) return;
        entry.target.classList.add("in");
        revealIO.unobserve(entry.target);
        if (entry.target.hasAttribute("data-count-to")) countUp(entry.target);
        qq("[data-count-to]", entry.target).forEach(countUp);
      });
    },
    { threshold: 0.15, rootMargin: "0px 0px -8% 0px" }
  );

  const watch = (el, delay = 0) => {
    if (delay) el.style.setProperty("--d", `${delay}s`);
    revealIO.observe(el);
  };

  qq("[data-reveal]").forEach((el) => watch(el));

  // book cards are re-rendered by app.js on every filter change
  function observeCards(root) {
    qq(".book-card:not(.in)", root).forEach((card, i) => {
      card.style.transitionDelay = `${Math.min(i, 12) * 55}ms`;
      revealIO.observe(card);
    });
  }
  const gridObserver = new MutationObserver(() => observeCards(document));
  qq("[data-books-grid], [data-favorites-grid]").forEach((grid) => {
    gridObserver.observe(grid, { childList: true });
  });
  observeCards(document);

  // ───────────────────────────────────────────────────────────────────────────
  // 6 · COUNT-UP
  // ───────────────────────────────────────────────────────────────────────────
  function countUp(el) {
    if (el.dataset.counted) return;
    el.dataset.counted = "1";
    const target = Number(el.dataset.countTo || 0);
    const suffix = el.dataset.countSuffix || "";
    // Write the true value first. If the animation never gets to run — reduced
    // motion, a background tab, a throttled frame loop — the number on screen is
    // still the right one rather than a stranded 0.
    el.textContent = target.toLocaleString() + suffix;
    if (reduced || document.hidden) return;
    const dur = 1700;
    const t0 = performance.now();
    (function step(now) {
      const p = clamp((now - t0) / dur);
      const eased = 1 - Math.pow(1 - p, 4);
      el.textContent = Math.round(target * eased).toLocaleString() + suffix;
      if (p < 1) requestAnimationFrame(step);
    })(t0);
  }

  // ───────────────────────────────────────────────────────────────────────────
  // 7 · 3D TILT CARDS — delegated, so re-rendered cards keep working
  // ───────────────────────────────────────────────────────────────────────────
  function initTilt() {
    if (!finePointer || reduced) return;
    const main = q("main");
    if (!main) return;
    let active = null;
    let rect = null;

    const reset = (card) => {
      card.classList.remove("tilting");
      card.style.setProperty("--rx", "0deg");
      card.style.setProperty("--ry", "0deg");
      card.style.setProperty("--sc", "1");
      card.style.setProperty("--ty", "0px");
    };

    main.addEventListener("pointermove", (e) => {
      const card = e.target.closest(".book-card");
      if (card !== active) {
        if (active) reset(active);
        active = card;
        if (card) {
          rect = card.getBoundingClientRect();
          card.classList.add("tilting");
          card.style.transitionDelay = "0ms";
        }
      }
      if (!card || !rect) return;

      const px = (e.clientX - rect.left) / rect.width;
      const py = (e.clientY - rect.top) / rect.height;
      card.style.setProperty("--ry", `${(px - 0.5) * 17}deg`);
      card.style.setProperty("--rx", `${(0.5 - py) * 15}deg`);
      card.style.setProperty("--sc", "1.04");
      card.style.setProperty("--ty", "-8px");
      card.style.setProperty("--gx", `${px * 100}%`);
      card.style.setProperty("--gy", `${py * 100}%`);
    }, { passive: true });

    main.addEventListener("pointerleave", () => {
      if (active) reset(active);
      active = null;
    }, true);
  }
  initTilt();

  // ───────────────────────────────────────────────────────────────────────────
  // 8 · SCROLL ORCHESTRATION
  //     progress bar
  // ───────────────────────────────────────────────────────────────────────────
  function initScroll() {
    const progress = q("[data-progress] i");

    function frame() {
      requestAnimationFrame(frame);
      if (document.hidden) return;

      const y = scrollY;
      const docH = document.documentElement.scrollHeight - innerHeight;

      // progress bar
      if (progress) progress.style.setProperty("--p", clamp(docH > 0 ? y / docH : 0).toFixed(4));



    }
    requestAnimationFrame(frame);
  }
  if (!reduced) initScroll();

  // ───────────────────────────────────────────────────────────────────────────
  // 9 · PAGE TRANSITIONS — curtain wipe whenever app.js swaps the active page
  // ───────────────────────────────────────────────────────────────────────────
  function initPageTransitions() {
    const curtain = q("[data-curtain]");
    if (curtain) qq("i", curtain).forEach((bar, i) => bar.style.setProperty("--i", i));

    const pages = qq(".page");
    let current = q(".page.active");

    const observer = new MutationObserver((mutations) => {
      for (const m of mutations) {
        const el = m.target;
        if (!el.classList.contains("active") || el === current) continue;
        current = el;

        if (!reduced && curtain) {
          curtain.classList.remove("wipe");
          void curtain.offsetWidth;
          curtain.classList.add("wipe");
        }
        el.classList.remove("entering");
        void el.offsetWidth;
        el.classList.add("entering");
        setTimeout(() => el.classList.remove("entering"), 900);

        // freshly shown grids need their reveal observers
        observeCards(el);
      }
    });

    pages.forEach((page) => observer.observe(page, { attributes: true, attributeFilter: ["class"] }));
  }
  initPageTransitions();

  // ───────────────────────────────────────────────────────────────────────────
  // 10 · SMOOTH ANCHORS
  // ───────────────────────────────────────────────────────────────────────────
  document.addEventListener("click", (e) => {
    const trigger = e.target.closest("[data-scroll-to]");
    if (!trigger) return;
    const target = q(trigger.dataset.scrollTo);
    if (!target) return;
    const top = target.getBoundingClientRect().top + scrollY - 90;
    scrollTo({ top, behavior: reduced ? "auto" : "smooth" });
  });

  // ───────────────────────────────────────────────────────────────────────────
  // 11 · HERO EMBERS — a few DOM motes for depth in front of the canvas
  // ───────────────────────────────────────────────────────────────────────────
  function initEmbers() {
    const host = q("[data-embers]");
    if (!host || reduced) return;
    const style = document.createElement("style");
    style.textContent = `
      .site-embers { position:absolute; inset:0; z-index:0; pointer-events:none; overflow:hidden; }
      .ember { position:absolute; bottom:-12px; width:3px; height:3px; border-radius:50%;
               background:#ffd98a; box-shadow:0 0 10px 2px rgba(255,201,110,.6);
               animation: ember-rise linear infinite; }
      @keyframes ember-rise {
        0%   { transform: translate3d(0,0,0) scale(.6); opacity:0; }
        12%  { opacity:.9; }
        100% { transform: translate3d(var(--dx,20px), calc(-100svh - 40px), 0) scale(1.1); opacity:0; }
      }`;
    document.head.appendChild(style);

    for (let i = 0; i < 18; i++) {
      const ember = document.createElement("span");
      ember.className = "ember";
      ember.style.left = `${Math.random() * 100}%`;
      ember.style.setProperty("--dx", `${(Math.random() - 0.5) * 160}px`);
      ember.style.animationDuration = `${9 + Math.random() * 12}s`;
      ember.style.animationDelay = `${-Math.random() * 18}s`;
      ember.style.opacity = `${0.35 + Math.random() * 0.5}`;
      host.appendChild(ember);
    }
  }
  initEmbers();
})();
