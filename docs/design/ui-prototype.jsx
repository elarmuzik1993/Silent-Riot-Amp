/*  STATUS (2026-10-23): early visual reference, frozen since 2026-06. NOT the current UI.
    It predates the arc knob, the live tuner, the EFFECTS panel (utility/boost/delay/reverb)
    and the INPUT/OUTPUT trims. The C++ GUI in src/gui/ is the source of truth.            */
import { useState, useRef, useCallback, useEffect, useMemo } from "react";

/*══════════════════════════════════════════════════════════
  SILENT RIOT — Studio Dark · Final UI
  Knob C (Notch Pointer) · Collapsible IR Loader
  Collapsible Advanced PSU · Monolit Beatz · 2026
══════════════════════════════════════════════════════════*/

const C = {
  bg:         "#08080C",
  bg2:        "#0B0B10",
  surface:    "#0F0F15",
  panel:      "#111118",
  panelLight: "#14141C",
  border:     "#1A1A24",
  borderHi:   "#252533",
  track:      "#1C1C28",
  muted:      "#2E2E3C",
  dim:        "#3C3C50",
  label:      "#4A4A5E",
  text:       "#9898AE",
  bright:     "#D0D0DC",
  white:      "#EAEAF0",
  accent:     "#FF6600",
  accentDim:  "#CC5200",
  accentLo:   "#993D00",
  accentGlow: "rgba(255,102,0,0.12)",
  green:      "#2DD4A0",
  greenDim:   "#1A8060",
  yellow:     "#E8C840",
  red:        "#E84040",
  teal:       "#00CCB0",
  tealDim:    "#009980",
};

const FONT = "'Space Mono', 'SF Mono', 'Fira Code', monospace";
const FONT_UI = "'DM Sans', 'SF Pro Display', system-ui, sans-serif";
const START = -135, SWEEP = 270;

/* ── Helpers ───────────────────────────────────────────── */
function polar(cx, cy, r, deg) {
  const rad = ((deg - 90) * Math.PI) / 180;
  return { x: cx + r * Math.cos(rad), y: cy + r * Math.sin(rad) };
}
function arcPath(cx, cy, r, s, e) {
  const p1 = polar(cx, cy, r, s), p2 = polar(cx, cy, r, e);
  return `M ${p1.x} ${p1.y} A ${r} ${r} 0 ${e - s > 180 ? 1 : 0} 1 ${p2.x} ${p2.y}`;
}
function useDrag(value, min, max, onChange) {
  const dragRef = useRef({ dragging: false, startY: 0, startVal: 0 });
  return useCallback((e) => {
    e.preventDefault();
    dragRef.current = { dragging: true, startY: e.clientY, startVal: value };
    const onMove = (ev) => {
      if (!dragRef.current.dragging) return;
      const dy = dragRef.current.startY - ev.clientY;
      const newVal = Math.min(max, Math.max(min, dragRef.current.startVal + dy * (max - min) / 160));
      onChange(Math.round(newVal * 10) / 10);
    };
    const onUp = () => {
      dragRef.current.dragging = false;
      window.removeEventListener("pointermove", onMove);
      window.removeEventListener("pointerup", onUp);
    };
    window.addEventListener("pointermove", onMove);
    window.addEventListener("pointerup", onUp);
  }, [value, min, max, onChange]);
}

/* ── IR Library ────────────────────────────────────────── */
const IR_LIBRARY = [
  { id: "cab_1x12_v30",   name: "1×12 V30",       mic: "SM57",  category: "STOCK",     samples: 1024, sampleRate: 48000 },
  { id: "cab_1x12_g12h",  name: "1×12 G12H",      mic: "SM57",  category: "STOCK",     samples: 1024, sampleRate: 48000 },
  { id: "ppc_2x12_v30",  name: "PPC 2×12 V30",       mic: "R121",  category: "STOCK",     samples: 2048, sampleRate: 48000 },
  { id: "ppc_4x12_v30",  name: "PPC 4×12 V30",       mic: "MD421", category: "ORANGE",    samples: 2048, sampleRate: 48000 },
  { id: "ppc_4x12_g12t", name: "PPC 4×12 G12T-75",   mic: "U87",   category: "ORANGE",    samples: 2048, sampleRate: 48000 },
  { id: "mesa_os_412",   name: "Mesa OS 4×12",        mic: "SM57",  category: "3RD PARTY", samples: 4096, sampleRate: 48000 },
  { id: "marsh_1960a",   name: "1960A G12T-75",       mic: "E906",  category: "3RD PARTY", samples: 2048, sampleRate: 48000 },
  { id: "fender_dlx",    name: "Deluxe Reverb 1×12",  mic: "SM57",  category: "3RD PARTY", samples: 1024, sampleRate: 48000 },
];

function generateIRWaveform(name, length = 120) {
  let seed = 0;
  for (let i = 0; i < name.length; i++) seed = ((seed << 5) - seed + name.charCodeAt(i)) | 0;
  const rng = () => { seed = (seed * 16807 + 0) % 2147483647; return (seed & 0x7fffffff) / 0x7fffffff; };
  const data = [];
  let amp = 0.9 + rng() * 0.1;
  const decay = 0.02 + rng() * 0.04;
  const freq = 3 + rng() * 8;
  const phase = rng() * Math.PI * 2;
  for (let i = 0; i < length; i++) {
    const t = i / length;
    const env = amp * Math.exp(-t * (4 + decay * 60));
    const osc = Math.sin(t * freq * Math.PI * 2 + phase) * (0.6 + rng() * 0.4);
    const noise = (rng() - 0.5) * 0.15 * Math.exp(-t * 8);
    data.push(env * osc + noise * env);
  }
  return data;
}

/* ══════════════════════════════════════════════════════════
   KNOB C — Notch Pointer
══════════════════════════════════════════════════════════ */
function Knob({ value, onChange, label, min = 0, max = 10, size = 80 }) {
  const onPointerDown = useDrag(value, min, max, onChange);
  const ctr = size / 2;
  const norm = (value - min) / (max - min);
  const angle = START + norm * SWEEP;
  const knobR = size * 0.28;
  const arcR = size * 0.36;
  const labelR = size * 0.46;

  const tipPos = polar(ctr, ctr, knobR + 1, angle);
  const leftPos = polar(ctr, ctr, knobR - 7, angle - 8);
  const rightPos = polar(ctr, ctr, knobR - 7, angle + 8);

  return (
    <div style={{ display: "flex", flexDirection: "column", alignItems: "center", gap: 5, userSelect: "none" }}>
      <svg width={size} height={size} viewBox={`0 0 ${size} ${size}`}
        onPointerDown={onPointerDown} style={{ cursor: "ns-resize", touchAction: "none" }}>
        {/* Numbered scale */}
        {Array.from({ length: 11 }).map((_, i) => {
          const deg = START + (i / 10) * SWEEP;
          const p = polar(ctr, ctr, labelR, deg);
          return (
            <text key={i} x={p.x} y={p.y} textAnchor="middle" dominantBaseline="central"
              fill={C.dim} fontSize={7} fontFamily={FONT}>{i}</text>
          );
        })}
        {/* Segmented arc */}
        {Array.from({ length: 20 }).map((_, i) => {
          const segStart = START + (i / 20) * SWEEP;
          const segEnd = segStart + SWEEP / 20 - 3;
          const active = (i + 1) / 20 <= norm;
          return (
            <path key={i} d={arcPath(ctr, ctr, arcR, segStart, segEnd)}
              fill="none" stroke={active ? C.accent : C.track} strokeWidth={3}
              strokeLinecap="butt" opacity={active ? 1 : 0.5} />
          );
        })}
        {/* Knob body */}
        <circle cx={ctr} cy={ctr} r={knobR} fill={C.surface} stroke={C.border} strokeWidth={1} />
        {/* Notch pointer */}
        <polygon points={`${tipPos.x},${tipPos.y} ${leftPos.x},${leftPos.y} ${rightPos.x},${rightPos.y}`}
          fill={C.accent} opacity={0.9} />
      </svg>
      {/* Readout */}
      <div style={{
        fontSize: 11, fontFamily: FONT, color: C.bright, letterSpacing: 0.5,
        background: C.surface, border: `1px solid ${C.border}`,
        padding: "2px 10px", borderRadius: 2, minWidth: 44, textAlign: "center",
      }}>{value.toFixed(1)}</div>
      {/* Label */}
      <div style={{ fontSize: 9, fontFamily: FONT, color: C.label, letterSpacing: 3, fontWeight: 500 }}>{label}</div>
    </div>
  );
}

/* ── VU Meter ──────────────────────────────────────────── */
function VUMeter({ level, label, height = 150 }) {
  const [anim, setAnim] = useState(0);
  useEffect(() => {
    let f;
    const go = () => {
      setAnim(p => p + (level + (Math.random() - 0.5) * 0.04 - p) * 0.12);
      f = requestAnimationFrame(go);
    };
    f = requestAnimationFrame(go);
    return () => cancelAnimationFrame(f);
  }, [level]);

  const barH = height - 18;
  const segs = 24;
  return (
    <div style={{ display: "flex", flexDirection: "column", alignItems: "center", gap: 5 }}>
      <div style={{
        width: 10, height: barH, background: C.surface, borderRadius: 5,
        border: `1px solid ${C.border}`, position: "relative", overflow: "hidden",
      }}>
        {Array.from({ length: segs }).map((_, i) => {
          const posN = (i + 1) / segs;
          const active = posN <= anim;
          let col = C.green;
          if (posN > 0.85) col = C.red;
          else if (posN > 0.7) col = C.yellow;
          return (
            <div key={i} style={{
              position: "absolute", bottom: `${(i / segs) * 100}%`, left: 2, right: 2,
              height: `${(1 / segs) * 100 - 1.5}%`, borderRadius: 1,
              background: active ? col : C.track,
              opacity: active ? 0.9 : 0.3,
            }} />
          );
        })}
      </div>
      <span style={{ fontSize: 8, fontFamily: FONT, color: C.label, letterSpacing: 2 }}>{label}</span>
    </div>
  );
}

/* ── Toggle Switch ─────────────────────────────────────── */
function Toggle({ label, value, onChange, activeColor = C.accent }) {
  return (
    <div style={{ display: "flex", flexDirection: "column", alignItems: "center", gap: 5 }}>
      <span style={{ fontSize: 8, fontFamily: FONT, color: C.label, letterSpacing: 2.5 }}>{label}</span>
      <div onClick={() => onChange(!value)} style={{
        width: 34, height: 16, borderRadius: 8, cursor: "pointer",
        background: value ? activeColor : C.track,
        border: `1px solid ${value ? activeColor + "60" : C.border}`,
        position: "relative", transition: "all 0.2s ease",
        boxShadow: value ? `0 0 10px ${activeColor}25` : "none",
      }}>
        <div style={{
          width: 12, height: 12, borderRadius: "50%",
          background: value ? "#fff" : C.dim,
          position: "absolute", top: 1, left: value ? 19 : 1,
          transition: "all 0.2s cubic-bezier(0.34,1.56,0.64,1)",
          boxShadow: value ? "0 1px 4px rgba(0,0,0,0.3)" : "none",
        }} />
      </div>
    </div>
  );
}

/* ── Preset Selector ───────────────────────────────────── */
function PresetSelector({ preset, presets, onChange }) {
  const idx = presets.indexOf(preset);
  const prev = () => onChange(presets[(idx - 1 + presets.length) % presets.length]);
  const next = () => onChange(presets[(idx + 1) % presets.length]);
  return (
    <div style={{ display: "flex", alignItems: "center", border: `1px solid ${C.border}`, borderRadius: 3, overflow: "hidden", height: 26 }}>
      <button onClick={prev} style={{
        background: C.surface, border: "none", borderRight: `1px solid ${C.border}`,
        color: C.label, fontSize: 11, padding: "0 8px", cursor: "pointer", height: "100%", fontFamily: FONT,
      }}>◂</button>
      <div style={{
        padding: "0 16px", fontFamily: FONT, fontSize: 10, color: C.bright, letterSpacing: 0.5,
        background: C.surface, display: "flex", alignItems: "center", height: "100%",
        minWidth: 140, justifyContent: "center",
      }}>{preset}</div>
      <button onClick={next} style={{
        background: C.surface, border: "none", borderLeft: `1px solid ${C.border}`,
        color: C.label, fontSize: 11, padding: "0 8px", cursor: "pointer", height: "100%", fontFamily: FONT,
      }}>▸</button>
    </div>
  );
}

/* ── Collapsible Section Bar ───────────────────────────── */
function SectionBar({ label, isOpen, onClick, activeColor = C.accent, hint }) {
  return (
    <div onClick={onClick} style={{
      display: "flex", alignItems: "center", justifyContent: "center",
      gap: 8, padding: "6px 20px", cursor: "pointer",
      background: isOpen ? C.panelLight : C.panel,
      borderTop: `1px solid ${C.border}`,
      transition: "background 0.2s",
    }}>
      <span style={{
        fontSize: 8, fontFamily: FONT,
        color: isOpen ? activeColor : C.dim,
        letterSpacing: 2.5, fontWeight: 700,
      }}>{label}</span>
      <span style={{
        fontSize: 10, color: C.label,
        transform: isOpen ? "rotate(180deg)" : "rotate(0deg)",
        transition: "transform 0.3s cubic-bezier(0.16,1,0.3,1)",
        display: "inline-block",
      }}>▾</span>
      {hint && !isOpen && (
        <span style={{ fontSize: 8, fontFamily: FONT, color: C.muted, marginLeft: 2 }}>{hint}</span>
      )}
    </div>
  );
}

/* ── PSU Supply Panel ──────────────────────────────────── */
function PSUSupply() {
  const [v, setV] = useState({ bp: 0.92, scr: 0.88, pi: 0.85, pre: 0.80 });
  useEffect(() => {
    const iv = setInterval(() => {
      setV(p => ({
        bp: Math.min(1, Math.max(0, p.bp + (Math.random() - 0.5) * 0.02)),
        scr: Math.min(1, Math.max(0, p.scr + (Math.random() - 0.5) * 0.015)),
        pi: Math.min(1, Math.max(0, p.pi + (Math.random() - 0.5) * 0.018)),
        pre: Math.min(1, Math.max(0, p.pre + (Math.random() - 0.5) * 0.012)),
      }));
    }, 120);
    return () => clearInterval(iv);
  }, []);

  const rails = [
    { k: "B+", val: v.bp, volt: "315V" },
    { k: "SCR", val: v.scr, volt: "282V" },
    { k: "PI", val: v.pi, volt: "260V" },
    { k: "PRE", val: v.pre, volt: "198V" },
  ];

  return (
    <div>
      <div style={{ fontSize: 7, fontFamily: FONT, color: C.accentLo, letterSpacing: 2.5, fontWeight: 700, marginBottom: 8 }}>SUPPLY RAILS</div>
      {rails.map(r => (
        <div key={r.k} style={{ display: "flex", alignItems: "center", gap: 6, marginBottom: 5 }}>
          <span style={{ fontSize: 8, fontFamily: FONT, color: C.label, width: 26 }}>{r.k}</span>
          <div style={{ flex: 1, height: 4, background: C.track, borderRadius: 2, overflow: "hidden" }}>
            <div style={{ width: `${r.val * 100}%`, height: "100%", borderRadius: 2, background: `linear-gradient(90deg, ${C.tealDim}, ${C.teal})`, transition: "width 0.1s" }} />
          </div>
          <span style={{ fontSize: 7, fontFamily: FONT, color: C.dim, width: 30, textAlign: "right" }}>{r.volt}</span>
        </div>
      ))}
    </div>
  );
}

/* ── PSU State Panel ───────────────────────────────────── */
function PSUState() {
  return (
    <div>
      <div style={{ fontSize: 7, fontFamily: FONT, color: C.accentLo, letterSpacing: 2.5, fontWeight: 700, marginBottom: 8 }}>ENGINE STATE</div>
      {[
        { k: "SAG", v: "24%" }, { k: "CLASS", v: "AB" }, { k: "OS", v: "4×" },
        { k: "BIAS", v: "-13.2V" }, { k: "TEMP", v: "~420°C" }, { k: "GRID I", v: "0.12mA" },
      ].map(s => (
        <div key={s.k} style={{ display: "flex", justifyContent: "space-between", marginBottom: 4 }}>
          <span style={{ fontSize: 8, fontFamily: FONT, color: C.label }}>{s.k}</span>
          <span style={{ fontSize: 8, fontFamily: FONT, color: C.text }}>{s.v}</span>
        </div>
      ))}
    </div>
  );
}

/* ── IR Waveform ───────────────────────────────────────── */
function IRWaveform({ irName, width = 320, height = 90 }) {
  const waveform = useMemo(() => generateIRWaveform(irName, width), [irName, width]);
  const maxAbs = Math.max(...waveform.map(Math.abs), 0.001);
  const mid = height / 2;
  const pathD = waveform.map((v, i) => `${i === 0 ? "M" : "L"} ${i} ${mid - (v / maxAbs) * (mid - 6)}`).join(" ");
  const envD = waveform.map((v, i) => `${i === 0 ? "M" : "L"} ${i} ${mid - (Math.abs(v) / maxAbs) * (mid - 6)}`).join(" ");

  return (
    <svg width={width} height={height} viewBox={`0 0 ${width} ${height}`} style={{ display: "block" }}>
      <rect width={width} height={height} fill={C.surface} />
      <line x1={0} y1={mid} x2={width} y2={mid} stroke={C.border} strokeWidth={0.5} />
      <line x1={0} y1={mid * 0.5} x2={width} y2={mid * 0.5} stroke={C.border} strokeWidth={0.3} strokeDasharray="3 3" />
      <line x1={0} y1={mid * 1.5} x2={width} y2={mid * 1.5} stroke={C.border} strokeWidth={0.3} strokeDasharray="3 3" />
      {[0.25, 0.5, 0.75].map(t => (
        <line key={t} x1={t * width} y1={0} x2={t * width} y2={height} stroke={C.border} strokeWidth={0.3} strokeDasharray="3 3" />
      ))}
      <path d={envD + ` L ${width} ${mid} L 0 ${mid} Z`} fill={C.accent} opacity={0.06} />
      <path d={pathD} fill="none" stroke={C.accent} strokeWidth={1.2} opacity={0.85} />
      <text x={5} y={12} fill={C.dim} fontSize={7} fontFamily={FONT}>IR IMPULSE</text>
      <text x={width - 5} y={height - 6} textAnchor="end" fill={C.dim} fontSize={7} fontFamily={FONT}>
        {(waveform.length / 48).toFixed(1)}ms
      </text>
    </svg>
  );
}

/* ── IR Loader Panel ───────────────────────────────────── */
function IRLoaderPanel({ isOpen, selectedIR, onSelectIR }) {
  const [activeCategory, setActiveCategory] = useState("ALL");
  const [hoveredIR, setHoveredIR] = useState(null);
  const [userFiles, setUserFiles] = useState([]);
  const [dragOver, setDragOver] = useState(false);

  const categories = ["ALL", "STOCK", "ORANGE", "3RD PARTY", "USER"];
  const filteredIRs = activeCategory === "ALL"
    ? [...IR_LIBRARY, ...userFiles]
    : activeCategory === "USER" ? userFiles
    : IR_LIBRARY.filter(ir => ir.category === activeCategory);

  const handleFileDrop = useCallback((e) => {
    e.preventDefault(); e.stopPropagation(); setDragOver(false);
    const files = Array.from(e.dataTransfer?.files || []);
    const wavFiles = files.filter(f => f.name.endsWith(".wav") || f.name.endsWith(".ir"));
    const newIRs = wavFiles.map((f, i) => ({
      id: `user_${Date.now()}_${i}`, name: f.name.replace(/\.(wav|ir)$/, ""),
      mic: "—", category: "USER", samples: 2048, sampleRate: 48000,
    }));
    setUserFiles(prev => [...prev, ...newIRs]);
    if (newIRs.length > 0) onSelectIR(newIRs[0]);
  }, [onSelectIR]);

  return (
    <div style={{
      maxHeight: isOpen ? 400 : 0, opacity: isOpen ? 1 : 0, overflow: "hidden",
      transition: "max-height 0.4s cubic-bezier(0.16,1,0.3,1), opacity 0.3s ease",
    }}>
      <div style={{ padding: "14px 20px 18px" }}>
        {/* Header */}
        <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", marginBottom: 12 }}>
          <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
            <span style={{ fontSize: 8, fontFamily: FONT, color: C.accentLo, letterSpacing: 2.5, fontWeight: 700 }}>CABINET IR</span>
            {selectedIR && (
              <span style={{
                fontSize: 9, fontFamily: FONT, color: C.text,
                background: C.surface, border: `1px solid ${C.border}`,
                padding: "2px 8px", borderRadius: 2,
              }}>{selectedIR.name}</span>
            )}
          </div>
          <span style={{ fontSize: 8, fontFamily: FONT, color: C.dim }}>
            {selectedIR ? `${selectedIR.samples} smp · ${selectedIR.sampleRate / 1000}kHz` : "NO IR LOADED"}
          </span>
        </div>

        {/* Two-column: browser | waveform */}
        <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 14 }}>
          {/* Left: Browser */}
          <div style={{ display: "flex", flexDirection: "column", gap: 8 }}>
            {/* Category tabs */}
            <div style={{ display: "flex", border: `1px solid ${C.border}`, borderRadius: 2, overflow: "hidden" }}>
              {categories.map(cat => (
                <button key={cat} onClick={() => setActiveCategory(cat)} style={{
                  flex: 1, padding: "3px 0", border: "none", cursor: "pointer",
                  fontSize: 7, fontFamily: FONT, letterSpacing: 1, fontWeight: 600,
                  background: activeCategory === cat ? C.accent : C.surface,
                  color: activeCategory === cat ? "#fff" : C.label,
                  borderRight: `1px solid ${C.border}`, transition: "all 0.15s ease",
                }}>{cat}</button>
              ))}
            </div>

            {/* IR list */}
            <div className="ir-list" style={{
              height: 180, overflowY: "auto", borderRadius: 3,
              border: `1px solid ${C.border}`, background: C.surface,
            }}>
              {/* Table header */}
              <div style={{
                display: "grid", gridTemplateColumns: "1fr 50px 56px",
                padding: "5px 10px", borderBottom: `1px solid ${C.border}`,
                position: "sticky", top: 0, background: C.panelLight, zIndex: 1,
              }}>
                <span style={{ fontSize: 7, fontFamily: FONT, color: C.dim, letterSpacing: 1.5 }}>NAME</span>
                <span style={{ fontSize: 7, fontFamily: FONT, color: C.dim, letterSpacing: 1.5 }}>MIC</span>
                <span style={{ fontSize: 7, fontFamily: FONT, color: C.dim, letterSpacing: 1.5, textAlign: "right" }}>SMP</span>
              </div>
              {filteredIRs.length === 0 ? (
                <div style={{ padding: 20, textAlign: "center", fontSize: 9, fontFamily: FONT, color: C.dim }}>
                  {activeCategory === "USER" ? "Drop .wav files to add IRs" : "No IRs in this category"}
                </div>
              ) : filteredIRs.map(ir => {
                const isSel = selectedIR?.id === ir.id;
                const isHov = hoveredIR === ir.id;
                return (
                  <div key={ir.id} onClick={() => onSelectIR(ir)}
                    onMouseEnter={() => setHoveredIR(ir.id)} onMouseLeave={() => setHoveredIR(null)}
                    style={{
                      display: "grid", gridTemplateColumns: "1fr 50px 56px",
                      padding: "6px 10px", cursor: "pointer",
                      background: isSel ? `${C.accent}12` : isHov ? C.panelLight : "transparent",
                      borderBottom: `1px solid ${C.border}`,
                      borderLeft: isSel ? `2px solid ${C.accent}` : "2px solid transparent",
                      transition: "all 0.1s ease",
                    }}>
                    <span style={{ fontSize: 9, fontFamily: FONT, color: isSel ? C.bright : C.text, fontWeight: isSel ? 600 : 400 }}>{ir.name}</span>
                    <span style={{ fontSize: 8, fontFamily: FONT, color: C.label }}>{ir.mic}</span>
                    <span style={{ fontSize: 8, fontFamily: FONT, color: C.dim, textAlign: "right" }}>{ir.samples}</span>
                  </div>
                );
              })}
            </div>

            {/* Drop zone */}
            <div onDragOver={(e) => { e.preventDefault(); setDragOver(true); }}
              onDragLeave={() => setDragOver(false)} onDrop={handleFileDrop}
              style={{
                padding: "8px 0", border: `1px dashed ${dragOver ? C.accent : C.borderHi}`,
                borderRadius: 3, textAlign: "center", cursor: "pointer",
                background: dragOver ? `${C.accent}08` : "transparent", transition: "all 0.2s ease",
              }}>
              <span style={{ fontSize: 8, fontFamily: FONT, color: dragOver ? C.accent : C.dim, letterSpacing: 1.5 }}>
                {dragOver ? "DROP TO LOAD IR" : "DROP .WAV OR CLICK TO BROWSE"}
              </span>
            </div>
          </div>

          {/* Right: Waveform + details */}
          <div style={{ display: "flex", flexDirection: "column", gap: 8 }}>
            <div style={{ border: `1px solid ${C.border}`, borderRadius: 3, overflow: "hidden" }}>
              {selectedIR ? (
                <IRWaveform irName={selectedIR.name} width={320} height={90} />
              ) : (
                <div style={{ width: 320, height: 90, background: C.surface, display: "flex", alignItems: "center", justifyContent: "center" }}>
                  <span style={{ fontSize: 9, fontFamily: FONT, color: C.muted }}>NO IR SELECTED</span>
                </div>
              )}
            </div>

            {selectedIR && (
              <div style={{
                background: C.surface, border: `1px solid ${C.border}`, borderRadius: 3, padding: "8px 12px",
                display: "grid", gridTemplateColumns: "1fr 1fr", gap: "4px 16px",
              }}>
                {[
                  { k: "FILE", v: selectedIR.name }, { k: "CATEGORY", v: selectedIR.category },
                  { k: "MIC", v: selectedIR.mic }, { k: "LENGTH", v: `${selectedIR.samples} smp` },
                  { k: "RATE", v: `${selectedIR.sampleRate / 1000} kHz` },
                  { k: "LATENCY", v: `${(selectedIR.samples / selectedIR.sampleRate * 1000).toFixed(1)} ms` },
                ].map(d => (
                  <div key={d.k} style={{ display: "flex", justifyContent: "space-between" }}>
                    <span style={{ fontSize: 7, fontFamily: FONT, color: C.label, letterSpacing: 1 }}>{d.k}</span>
                    <span style={{ fontSize: 8, fontFamily: FONT, color: C.text }}>{d.v}</span>
                  </div>
                ))}
              </div>
            )}

            {selectedIR && (
              <div style={{ display: "flex", gap: 5 }}>
                {["PHASE INV", "NORMALIZE", "TRIM"].map(action => (
                  <button key={action} style={{
                    flex: 1, padding: "6px 0", border: `1px solid ${C.border}`,
                    borderRadius: 3, background: C.surface, cursor: "pointer",
                    fontSize: 7, fontFamily: FONT, color: C.label, letterSpacing: 1.5,
                    transition: "all 0.15s",
                  }}>{action}</button>
                ))}
              </div>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}

/* ── Settings Toggle Row ────────────────────────────────── */
function SettingsToggle({ label, desc, value, onChange }) {
  return (
    <div style={{
      display: "flex", alignItems: "center", justifyContent: "space-between",
      padding: "8px 0",
      borderBottom: `1px solid ${C.border}`,
    }}>
      <div>
        <div style={{ fontSize: 10, fontFamily: FONT, color: C.bright, letterSpacing: 0.5 }}>{label}</div>
        {desc && <div style={{ fontSize: 8, color: C.dim, marginTop: 2 }}>{desc}</div>}
      </div>
      <div onClick={() => onChange(!value)} style={{
        width: 34, height: 16, borderRadius: 8, cursor: "pointer",
        background: value ? C.accent : C.track,
        border: `1px solid ${value ? C.accent + "60" : C.border}`,
        position: "relative", transition: "all 0.2s ease",
        flexShrink: 0, marginLeft: 16,
      }}>
        <div style={{
          width: 12, height: 12, borderRadius: "50%",
          background: value ? "#fff" : C.dim,
          position: "absolute", top: 1, left: value ? 19 : 1,
          transition: "all 0.2s cubic-bezier(0.34,1.56,0.64,1)",
        }} />
      </div>
    </div>
  );
}

/* ── Settings Select Row ───────────────────────────────── */
function SettingsSelect({ label, desc, value, options, onChange }) {
  return (
    <div style={{
      display: "flex", alignItems: "center", justifyContent: "space-between",
      padding: "8px 0",
      borderBottom: `1px solid ${C.border}`,
    }}>
      <div>
        <div style={{ fontSize: 10, fontFamily: FONT, color: C.bright, letterSpacing: 0.5 }}>{label}</div>
        {desc && <div style={{ fontSize: 8, color: C.dim, marginTop: 2 }}>{desc}</div>}
      </div>
      <div style={{ display: "flex", border: `1px solid ${C.border}`, borderRadius: 3, overflow: "hidden", flexShrink: 0, marginLeft: 16 }}>
        {options.map(opt => (
          <button key={opt} onClick={() => onChange(opt)} style={{
            padding: "3px 10px", border: "none", cursor: "pointer",
            fontSize: 8, fontFamily: FONT, letterSpacing: 0.5, fontWeight: 600,
            background: value === opt ? C.accent : C.surface,
            color: value === opt ? "#fff" : C.label,
            borderRight: `1px solid ${C.border}`,
            transition: "all 0.15s ease",
          }}>{opt}</button>
        ))}
      </div>
    </div>
  );
}

/* ── Settings Panel (overlay from header) ──────────────── */
function SettingsPanel({ isOpen, onClose, theme, setTheme, meterBallistics, setMeterBallistics,
  meterPeak, setMeterPeak, tooltips, setTooltips, knobMode, setKnobMode, uiScale, setUiScale }) {

  return (
    <div style={{
      position: "absolute", top: 38, right: 0, width: 340, zIndex: 50,
      maxHeight: isOpen ? 600 : 0, opacity: isOpen ? 1 : 0,
      overflow: "hidden",
      transition: "max-height 0.35s cubic-bezier(0.16,1,0.3,1), opacity 0.2s ease",
      pointerEvents: isOpen ? "auto" : "none",
    }}>
      <div style={{
        background: C.panel,
        border: `1px solid ${C.border}`,
        borderTop: "none",
        borderRadius: "0 0 6px 6px",
        boxShadow: "0 16px 48px rgba(0,0,0,0.5)",
      }}>
        {/* Panel header */}
        <div style={{
          display: "flex", alignItems: "center", justifyContent: "space-between",
          padding: "12px 16px 10px",
          borderBottom: `1px solid ${C.border}`,
        }}>
          <span style={{ fontSize: 9, fontFamily: FONT, color: C.accent, letterSpacing: 2.5, fontWeight: 700 }}>SETTINGS</span>
          <div onClick={onClose} style={{ cursor: "pointer", padding: "2px 6px", borderRadius: 3, background: C.surface, border: `1px solid ${C.border}` }}>
            <span style={{ fontSize: 10, color: C.label, fontFamily: FONT }}>✕</span>
          </div>
        </div>

        <div style={{ padding: "4px 16px 8px" }}>
          {/* ── UI Preferences ─────────────────────── */}
          <div style={{ fontSize: 7, fontFamily: FONT, color: C.accentLo, letterSpacing: 2.5, fontWeight: 700, padding: "10px 0 6px" }}>
            UI PREFERENCES
          </div>

          <SettingsSelect label="Theme" desc="Plugin colour scheme" value={theme}
            options={["Dark", "Midnight", "OLED"]} onChange={setTheme} />

          <SettingsSelect label="Meter Ballistics" desc="VU response speed" value={meterBallistics}
            options={["VU", "PPM", "K-20"]} onChange={setMeterBallistics} />

          <SettingsToggle label="Peak Hold" desc="Show peak indicator on meters" value={meterPeak} onChange={setMeterPeak} />

          <SettingsSelect label="Knob Mode" desc="Drag behaviour" value={knobMode}
            options={["Linear", "Circular"]} onChange={setKnobMode} />

          <SettingsToggle label="Tooltips" desc="Show parameter hints on hover" value={tooltips} onChange={setTooltips} />

          <SettingsSelect label="UI Scale" desc="Interface size" value={uiScale}
            options={["75%", "100%", "125%", "150%"]} onChange={setUiScale} />

          {/* ── Licensing / About ──────────────────── */}
          <div style={{ fontSize: 7, fontFamily: FONT, color: C.accentLo, letterSpacing: 2.5, fontWeight: 700, padding: "14px 0 6px" }}>
            ABOUT
          </div>

          <div style={{ padding: "6px 0", borderBottom: `1px solid ${C.border}` }}>
            {[
              { k: "PRODUCT", v: "Silent Riot" },
              { k: "VERSION", v: "1.0.0 build 247" },
              { k: "FORMAT", v: "VST3 / AU" },
              { k: "DEVELOPER", v: "Monolit Beatz" },
            ].map(r => (
              <div key={r.k} style={{ display: "flex", justifyContent: "space-between", marginBottom: 4 }}>
                <span style={{ fontSize: 8, fontFamily: FONT, color: C.label, letterSpacing: 1 }}>{r.k}</span>
                <span style={{ fontSize: 9, fontFamily: FONT, color: C.text }}>{r.v}</span>
              </div>
            ))}
          </div>

          {/* License status */}
          <div style={{
            margin: "10px 0 6px",
            padding: "10px 12px",
            background: C.surface,
            border: `1px solid ${C.greenDim}40`,
            borderRadius: 4,
            display: "flex", alignItems: "center", gap: 10,
          }}>
            <div style={{
              width: 8, height: 8, borderRadius: "50%", background: C.green, flexShrink: 0,
              boxShadow: `0 0 8px ${C.green}50`,
            }} />
            <div>
              <div style={{ fontSize: 9, fontFamily: FONT, color: C.green, fontWeight: 700, letterSpacing: 1 }}>LICENSED</div>
              <div style={{ fontSize: 7, fontFamily: FONT, color: C.dim, marginTop: 2 }}>Free &amp; open source — GPLv3</div>
            </div>
          </div>

          {/* Links */}
          <div style={{ display: "flex", gap: 6, padding: "8px 0 6px" }}>
            {["MANUAL", "SUPPORT", "CHANGELOG"].map(link => (
              <button key={link} style={{
                flex: 1, padding: "6px 0", border: `1px solid ${C.border}`,
                borderRadius: 3, background: C.surface, cursor: "pointer",
                fontSize: 7, fontFamily: FONT, color: C.label, letterSpacing: 1.5,
                transition: "all 0.15s",
              }}>{link}</button>
            ))}
          </div>
        </div>
      </div>
    </div>
  );
}

/* ══════════════════════════════════════════════════════════
   MAIN APP
══════════════════════════════════════════════════════════ */
export default function SilentRiotFinal() {
  const [gain, setGain] = useState(6.0);
  const [tone, setTone] = useState(5.0);
  const [volume, setVolume] = useState(5.0);
  const [halfPower, setHalfPower] = useState(false);
  const [cabSim, setCabSim] = useState(true);
  const [power, setPower] = useState(true);
  const [preset, setPreset] = useState("Classic Crunch");
  const [mounted, setMounted] = useState(false);
  const [irOpen, setIrOpen] = useState(false);
  const [advOpen, setAdvOpen] = useState(false);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [selectedIR, setSelectedIR] = useState(IR_LIBRARY[0]);
  const [theme, setTheme] = useState("dark");
  const [meterBallistics, setMeterBallistics] = useState("VU");
  const [meterPeak, setMeterPeak] = useState(true);
  const [tooltips, setTooltips] = useState(true);
  const [knobMode, setKnobMode] = useState("linear");
  const [uiScale, setUiScale] = useState("100%");

  const presets = ["Classic Crunch", "Edge of Breakup", "Full Drive", "Clean Shimmer", "Heavy Sag"];

  useEffect(() => {
    const link = document.createElement("link");
    link.href = "https://fonts.googleapis.com/css2?family=Space+Mono:wght@400;700&family=DM+Sans:wght@400;500;600;700&display=swap";
    link.rel = "stylesheet";
    document.head.appendChild(link);
    setTimeout(() => setMounted(true), 80);
    return () => document.head.removeChild(link);
  }, []);

  const handleCabToggle = useCallback((val) => {
    setCabSim(val);
    if (val && !irOpen) setIrOpen(true);
  }, [irOpen]);

  const inLevel = power ? (gain / 10) * 0.7 + 0.15 : 0;
  const outLevel = power ? (volume / 10) * 0.65 + 0.1 : 0;

  return (
    <div style={{
      minHeight: "100vh",
      background: `radial-gradient(ellipse 120% 80% at 50% 0%, #10101A 0%, ${C.bg} 60%)`,
      display: "flex", alignItems: "center", justifyContent: "center",
      padding: 20, fontFamily: FONT_UI,
    }}>
      <div style={{
        width: 700,
        background: `linear-gradient(180deg, ${C.bg2} 0%, ${C.bg} 100%)`,
        borderRadius: 6, border: `1px solid ${C.border}`,
        boxShadow: `0 24px 80px rgba(0,0,0,0.6), 0 0 1px rgba(255,255,255,0.04) inset`,
        opacity: mounted ? 1 : 0, transform: mounted ? "scale(1)" : "scale(0.98)",
        transition: "all 0.5s cubic-bezier(0.16,1,0.3,1)",
        position: "relative",
      }}>

        {/* ── Header ──────────────────────────────────────── */}
        <div style={{
          display: "flex", alignItems: "center", justifyContent: "space-between",
          padding: "0 20px", height: 38, background: C.panel,
          borderBottom: `1px solid ${C.border}`,
          position: "relative",
        }}>
          <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
            <span style={{ fontSize: 14, fontWeight: 700, fontFamily: FONT, color: C.accent, letterSpacing: -0.5 }}>SR</span>
            <div style={{ width: 1, height: 16, background: C.border }} />
            <span style={{ fontSize: 10, fontFamily: FONT, color: C.label, letterSpacing: 2 }}>SILENT RIOT</span>
          </div>
          {/* Absolutely centered preset */}
          <div style={{ position: "absolute", left: "50%", transform: "translateX(-50%)" }}>
            <PresetSelector preset={preset} presets={presets} onChange={setPreset} />
          </div>
          <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
            <span style={{ fontSize: 8, fontFamily: FONT, color: C.muted, letterSpacing: 2.5 }}>MONOLIT</span>
            {/* Gear icon */}
            <div onClick={() => setSettingsOpen(!settingsOpen)} style={{
              width: 24, height: 24, borderRadius: 4, cursor: "pointer",
              display: "flex", alignItems: "center", justifyContent: "center",
              background: settingsOpen ? `${C.accent}18` : "transparent",
              border: `1px solid ${settingsOpen ? C.accent + "40" : "transparent"}`,
              transition: "all 0.2s ease",
            }}>
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke={settingsOpen ? C.accent : C.label}
                strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"
                style={{ transition: "stroke 0.2s, transform 0.4s cubic-bezier(0.16,1,0.3,1)", transform: settingsOpen ? "rotate(60deg)" : "rotate(0deg)" }}>
                <circle cx="12" cy="12" r="3" />
                <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83-2.83l.06-.06A1.65 1.65 0 0 0 4.68 15a1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 2.83-2.83l.06.06A1.65 1.65 0 0 0 9 4.68a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 2.83l-.06.06A1.65 1.65 0 0 0 19.4 9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z" />
              </svg>
            </div>
          </div>
        </div>

        {/* ── Settings Panel (overlay) ─────────────────────── */}
        <SettingsPanel
          isOpen={settingsOpen} onClose={() => setSettingsOpen(false)}
          theme={theme} setTheme={setTheme}
          meterBallistics={meterBallistics} setMeterBallistics={setMeterBallistics}
          meterPeak={meterPeak} setMeterPeak={setMeterPeak}
          tooltips={tooltips} setTooltips={setTooltips}
          knobMode={knobMode} setKnobMode={setKnobMode}
          uiScale={uiScale} setUiScale={setUiScale}
        />

        {/* ── Main Body ───────────────────────────────────── */}
        <div style={{
          display: "grid", gridTemplateColumns: "40px 1fr 40px",
          padding: "28px 0", alignItems: "center", minHeight: 230,
        }}>
          {/* VU IN */}
          <div style={{ display: "flex", justifyContent: "center", borderRight: `1px solid ${C.border}`, height: "100%", alignItems: "center" }}>
            <VUMeter level={inLevel} label="IN" height={170} />
          </div>

          {/* Controls */}
          <div style={{ display: "flex", flexDirection: "column", alignItems: "center", gap: 24, padding: "0 24px" }}>
            <div style={{ display: "flex", alignItems: "flex-start", justifyContent: "center", gap: 36 }}>
              <Knob label="GAIN" value={gain} onChange={setGain} />
              <Knob label="TONE" value={tone} onChange={setTone} />
              <Knob label="VOLUME" value={volume} onChange={setVolume} />
            </div>
            <div style={{ width: 260, height: 1, background: `linear-gradient(90deg, transparent, ${C.border}, transparent)` }} />
            <div style={{ display: "flex", alignItems: "center", gap: 32 }}>
              <Toggle label="7W" value={halfPower} onChange={setHalfPower} />
              <Toggle label="CAB" value={cabSim} onChange={handleCabToggle} />
              <Toggle label="PWR" value={power} onChange={setPower} activeColor={C.green} />
            </div>
          </div>

          {/* VU OUT */}
          <div style={{ display: "flex", justifyContent: "center", borderLeft: `1px solid ${C.border}`, height: "100%", alignItems: "center" }}>
            <VUMeter level={outLevel} label="OUT" height={170} />
          </div>
        </div>

        {/* ── IR Loader Section ────────────────────────────── */}
        <SectionBar
          label="IR LOADER"
          isOpen={irOpen}
          onClick={() => setIrOpen(!irOpen)}
          activeColor={cabSim ? C.accent : C.dim}
          hint={selectedIR && !irOpen ? selectedIR.name : null}
        />
        <IRLoaderPanel isOpen={irOpen} selectedIR={selectedIR} onSelectIR={setSelectedIR} />

        {/* ── Advanced Section ─────────────────────────────── */}
        <SectionBar
          label="ADVANCED"
          isOpen={advOpen}
          onClick={() => setAdvOpen(!advOpen)}
        />
        <div style={{
          maxHeight: advOpen ? 180 : 0, opacity: advOpen ? 1 : 0, overflow: "hidden",
          transition: "max-height 0.35s cubic-bezier(0.16,1,0.3,1), opacity 0.25s ease",
        }}>
          <div style={{
            display: "grid", gridTemplateColumns: "1fr 1px 1fr",
            gap: 0, padding: "14px 24px 18px", background: C.panel,
          }}>
            <div style={{ paddingRight: 20 }}><PSUSupply /></div>
            <div style={{ background: C.border }} />
            <div style={{ paddingLeft: 20 }}><PSUState /></div>
          </div>
        </div>

        {/* ── Footer ──────────────────────────────────────── */}
        <div style={{
          display: "flex", alignItems: "center", justifyContent: "space-between",
          padding: "0 20px", height: 30, background: C.panel,
          borderTop: `1px solid ${C.border}`,
        }}>
          <div style={{ display: "flex", gap: 16 }}>
            <span style={{ fontSize: 9, fontFamily: FONT, color: C.dim }}>4× OS</span>
            <span style={{ fontSize: 9, fontFamily: FONT, color: C.dim }}>LATENCY <span style={{ color: C.text }}>2.9ms</span></span>
          </div>
          <div style={{ display: "flex", gap: 16 }}>
            <span style={{ fontSize: 9, fontFamily: FONT, color: C.dim }}>CPU <span style={{ color: C.text }}>11.2%</span></span>
            <span style={{ fontSize: 9, fontFamily: FONT, color: C.dim }}>48kHz / 32bit</span>
          </div>
        </div>
      </div>

      <style>{`
        .ir-list::-webkit-scrollbar {
          width: 6px;
        }
        .ir-list::-webkit-scrollbar-track {
          background: ${C.surface};
          border-radius: 3px;
        }
        .ir-list::-webkit-scrollbar-thumb {
          background: ${C.accentDim};
          border-radius: 3px;
        }
        .ir-list::-webkit-scrollbar-thumb:hover {
          background: ${C.accent};
        }
        .ir-list {
          scrollbar-width: thin;
          scrollbar-color: ${C.accentDim} ${C.surface};
        }
      `}</style>
    </div>
  );
}
