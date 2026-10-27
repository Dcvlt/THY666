/* Browser controls for the shared C firmware runtime. No device access. */
import {MENU_ROWS} from './studio-menu.js';
const FIRMWARE_PROFILE_KEY = 'thy333.firmware.scope.v1';
const RESPONSE_KEYS = ['attack', 'release', 'floor', 'ceiling', 'curve'];
const VELOCITY_KEYS = ['mode', 'curve', 'softRate', 'hardRate', 'minimum', 'maximum'];

export function validateFirmwareResponse(value) {
  if (!value || !RESPONSE_KEYS.slice(0, 4).every(key => Number.isInteger(value[key]) && value[key] >= 0 && value[key] <= 4095) ||
      !Number.isInteger(value.curve) || value.curve < 0 || value.curve > 2) return 'Use whole ADC values from 0 to 4095.';
  if (!(value.release < value.attack)) return 'Release must be lower than Trigger.';
  if (!(value.floor < value.ceiling)) return 'Aftertouch floor must be lower than Aftertouch max.';
  return null;
}

export function validateFirmwareReleasePolicy(value) {
  return !value || !Number.isInteger(value.linked) || value.linked<0 || value.linked>1 ||
    !Number.isInteger(value.gap) || value.gap<1 || value.gap>4095?'Choose Auto release ON/OFF and a whole gap from 1 to 4095 ADC.':null;
}

export function validateFirmwareVelocity(value) {
  const integer = (key, min, max) => Number.isInteger(value?.[key]) && value[key] >= min && value[key] <= max;
  if (!integer('mode',0,1) || !integer('curve',0,2) || !integer('softRate',0,4094) ||
      !integer('hardRate',1,4095) || !integer('minimum',1,127) || !integer('maximum',1,127))
    return 'Use whole rise rates from 0 to 4095 ADC/ms and MIDI velocities from 1 to 127.';
  if (value.softRate >= value.hardRate) return 'Soft rise must be lower than firm rise.';
  if (value.minimum > value.maximum) return 'Minimum velocity must not exceed maximum.';
  return null;
}

export function validateFirmwareExpressionProfile(value) {
  if (!value || typeof value.enabled !== 'boolean' || !Number.isInteger(value.split) || value.split < 10 || value.split > 90 ||
      !Number.isInteger(value.pressureCurve) || value.pressureCurve < 0 || value.pressureCurve > 2 ||
      !Number.isInteger(value.aftertouchCurve) || value.aftertouchCurve < 0 || value.aftertouchCurve > 2)
    return 'Choose Dual-stage ON/OFF, an Aftertouch start from 10 to 90%, and the listed curves.';
  return null;
}

/* Sampling coordinates are for the SVG only. The two axis values always come
 * from C's applied scope/profile; draft controls never alter the live engine. */
export function sampleFirmwareExpressionProfile(firmware, scope, points = 101) {
  if (!firmware?.expressionAxes || !Number.isInteger(scope) || scope < -2 || scope > 95 ||
      !Number.isInteger(points) || points < 2 || points > 513) return [];
  return Array.from({length:points},(_,index)=>{
    const raw=Math.round(index*4095/(points-1));
    return {raw,...firmware.expressionAxes(scope,raw)};
  });
}

function previewPressure(response, raw) {
  if (raw <= response.floor) return 0;
  if (raw >= response.ceiling) return 127;
  const x = Math.floor((raw - response.floor) * 127 / (response.ceiling - response.floor));
  return response.curve === 1 ? 127 - Math.floor((127 - x) ** 2 / 127) : response.curve === 2 ? Math.floor(x * x / 127) : x;
}

export function initializeFirmwareStudio({ engine, getContext, onEvents = () => {}, onChange = () => {}, selectPad = () => {} }) {
  if (!engine?.firmware) throw new Error('The shared firmware runtime is unavailable.');
  const instrument = document.querySelector('.instrument');
  if (!instrument) throw new Error('The controller preview is unavailable.');
  const firmware = engine.firmware;
  const panel = document.createElement('details');
  panel.className = 'panel firmware-studio';
  panel.open = true;
  panel.innerHTML = `
    <summary><span class="section-kicker">FIRMWARE <span>SENSITIVITY & LIGHT</span></span><span class="fs-summary-detail">Shared C runtime · browser preview</span><span class="fs-chevron" aria-hidden="true">⌄</span></summary>
    <div class="fs-intro"><p>Shape how the pads respond, then preview the result on the controller above.</p><span class="fs-local-tag">LOCAL PREVIEW</span></div>
    <div class="fs-columns">
      <section class="fs-section" aria-labelledby="fs-response-title">
        <h2 id="fs-response-title">Pad response</h2>
        <p class="fs-copy">Your shims change the feel. These are personal settings; no fitted velocity curve is assumed.</p>
        <label>Apply to<select id="fs-scope"><option value="left">Left cluster</option><option value="right">Right cluster</option><option value="pad">Selected pad only</option></select></label>
        <p class="fs-scope-note" id="fs-scope-note"></p>
        <form id="fs-sensitivity-form"><label>Sensitivity · higher is easier<input id="fs-amount" type="number" min="0" max="100" step="1" required></label>
          <div class="fs-actions"><button class="button button-small fs-primary" id="fs-amount-apply" type="submit">Apply sensitivity</button></div></form>
        <form id="fs-release-form"><label class="fs-switch"><input id="fs-linked-release" type="checkbox"><span>Auto release</span></label>
          <label>Release gap · ADC<input id="fs-release-gap" type="number" min="1" max="4095" step="1" required></label>
          <div class="fs-actions"><button class="button button-small" id="fs-release-apply" type="submit">Apply release policy</button></div></form>
        <p class="fs-hint">Sensitivity uses the same C setting as the device. Auto release follows Trigger with a safe gap. Advanced ADC values and aftertouch are independent.</p>
        <form id="fs-response-form">
          <div class="fs-fields">
            <label>Trigger · ADC<input id="fs-attack" name="attack" type="number" min="1" max="4095" step="1" required></label>
            <label>Release · ADC<input id="fs-release" name="release" type="number" min="0" max="4094" step="1" required></label>
            <label>Aftertouch floor<input id="fs-floor" name="floor" type="number" min="0" max="4094" step="1" required></label>
            <label>Aftertouch max<input id="fs-ceiling" name="ceiling" type="number" min="1" max="4095" step="1" required></label>
            <label class="fs-full">Aftertouch curve<select id="fs-curve" name="curve"><option value="0">Linear</option><option value="1">Soft · earlier expression</option><option value="2">Firm · later expression</option></select></label>
          </div>
          <svg id="fs-curve-chart" class="fs-curve-chart" viewBox="0 0 300 90" role="img" aria-label="Draft pressure response across the ADC range"><path class="fs-chart-axis" d="M1 1V75H299"/><path class="fs-chart-guide" d="M1 38H299"/><path id="fs-curve-line" class="fs-chart-line"/><path id="fs-gate-lines" class="fs-chart-gates"/><text x="1" y="89">0 ADC</text><text x="254" y="89">4095 ADC</text></svg>
          <div class="fs-actions"><button class="button button-small fs-primary" id="fs-apply" type="submit">Apply response</button><button class="button button-small" id="fs-inherit" type="button">Use this half's default</button></div>
        </form>
        <p class="fs-hint">Changing the raw Trigger/Release pair above selects Manual release. Aftertouch-only edits preserve Auto release. Trigger needs two fresh samples; response edits requalify quiet input.</p>
        <div class="fs-expression fs-dual-profile"><h3>Pressure & aftertouch</h3>
          <p class="fs-hint">Optional Aftertouch stage: pressure reaches its full 0–127 output at AT start, then Aftertouch rises through its own 0–127 range. Each stage is rescaled to its part of the calibrated range. OFF gives pressure the full range. MPE uses Channel Pressure and CC74.</p>
          <form id="fs-profile-form">
            <label class="fs-switch"><input id="fs-profile-enabled" type="checkbox"><span>Aftertouch stage</span></label>
            <div class="fs-fields">
              <label class="fs-full">Aftertouch start · %<input id="fs-profile-split" type="number" min="10" max="90" step="1" value="70" required></label>
              <label>Pressure curve<select id="fs-profile-pressureCurve"><option value="0">Linear</option><option value="1">Soft</option><option value="2">Firm</option></select></label>
              <label>Aftertouch curve<select id="fs-profile-aftertouchCurve"><option value="0">Linear</option><option value="1">Soft</option><option value="2">Firm</option></select></label>
            </div>
            <p class="fs-scope-note" id="fs-profile-scope"></p>
            <svg id="fs-profile-chart" class="fs-curve-chart" viewBox="0 0 300 90" role="img" aria-label="Applied C pressure and aftertouch response"><path class="fs-chart-axis" d="M1 1V75H299"/><path class="fs-chart-guide" d="M1 38H299"/><path id="fs-profile-pressure-line" class="fs-chart-line"/><path id="fs-profile-at-line" class="fs-profile-at-line"/><text x="1" y="89">0 ADC</text><text x="254" y="89">4095 ADC</text></svg>
            <p class="fs-hint">Applied response · gold pressure / green aftertouch when enabled. Apply edits to update the curves.</p>
            <div class="fs-actions"><button id="fs-profile-apply" class="button button-small fs-primary" type="submit">Apply expression profile</button><button id="fs-profile-inherit" class="button button-small" type="button">Use cluster profile</button></div>
          </form>
        </div>
        <div class="fs-expression"><h3>Strike velocity</h3><p class="fs-hint">Fixed uses the route velocity. Strike measures the ADC rise before the ordinary trigger confirmation, with no extra attack wait. Pressure stays independent.</p>
          <form id="fs-strike-form"><div class="fs-fields">
            <label>Velocity mode<select id="fs-strike-mode"><option value="0">Fixed</option><option value="1">Strike</option></select></label>
            <label>Strike curve<select id="fs-strike-curve"><option value="0">Linear</option><option value="1">Soft</option><option value="2">Firm</option></select></label>
            <label>Soft rise · ADC/ms<input id="fs-strike-softRate" type="number" min="0" max="4094" step="1" required></label>
            <label>Firm rise · ADC/ms<input id="fs-strike-hardRate" type="number" min="1" max="4095" step="1" required></label>
            <label>Minimum velocity<input id="fs-strike-minimum" type="number" min="1" max="127" step="1" required></label>
            <label>Maximum velocity<input id="fs-strike-maximum" type="number" min="1" max="127" step="1" required></label>
          </div><svg id="fs-strike-chart" class="fs-curve-chart" viewBox="0 0 300 90" role="img" aria-label="Shared C strike velocity curve"><path class="fs-chart-axis" d="M1 1V75H299"/><path id="fs-strike-line" class="fs-chart-line"/><text x="1" y="89">0 ADC/ms</text><text id="fs-strike-axis" x="210" y="89"></text></svg>
          <div class="fs-actions"><button class="button button-small fs-primary" id="fs-strike-apply" type="submit">Apply velocity</button><button class="button button-small" id="fs-strike-inherit" type="button">Use this half's velocity</button><button class="button button-small" id="fs-strike-reset" type="button">Reset velocity</button></div></form>
          <p class="fs-scope-note" id="fs-strike-scope"></p>
        </div>
      </section>
      <section class="fs-section" aria-labelledby="fs-light-title">
        <h2 id="fs-light-title">Notes into light</h2>
        <p class="fs-copy">Note colors follow the musical layout. Held pads brighten; release fades back to the layout.</p>
        <label class="fs-switch"><input id="fs-light-enabled" type="checkbox"><span>Pad illumination</span></label>
        <label class="fs-range-label">Idle brightness <output id="fs-idle-value"></output><input id="fs-idle" type="range" min="0" max="8" step="1"></label>
        <label class="fs-range-label">Held brightness <output id="fs-held-value"></output><input id="fs-held" type="range" min="0" max="8" step="1"></label>
        <label class="fs-range-label">Release fade <output id="fs-fade-value"></output><input id="fs-fade" type="range" min="0" max="500" step="10"></label>
        <div class="fs-expression"><h3>Sensor lab defaults</h3><label class="fs-switch"><input id="fs-pressure-enabled" type="checkbox"><span>Default Poly Aftertouch</span></label><label>Fixed fallback velocity<input id="fs-velocity" type="number" min="1" max="127" step="1"></label><p class="fs-hint">For playing, choose Aftertouch and Bend in Performance (also available in MIDI), or enable MPE in Performance. Explicit route choices override this lab default; MPE uses its own expression path. Pointer and keyboard pad presses have no sensor trajectory and use fixed route velocity; use the strike auditions to test C estimation.</p></div>
      </section>
      <section class="fs-section fs-sensor-section" aria-labelledby="fs-sensor-title">
        <div class="fs-sensor-heading"><h2 id="fs-sensor-title">Sensor lab</h2><span id="fs-sensor-state" class="fs-sensor-state">IDLE</span></div>
        <p class="fs-copy">Feed raw samples through the same detector as the firmware. The timeline shows its MIDI output.</p>
        <label>Electrical pad ID<input id="fs-pad" type="number" min="0" max="95" step="1"></label>
        <p class="fs-scope-note" id="fs-pad-location"></p>
        <div class="fs-raw-heading"><label for="fs-raw">Raw ADC input</label><output id="fs-raw-value">0</output></div>
        <input id="fs-raw" type="range" min="0" max="4095" step="1" value="0" disabled>
        <div class="fs-raw-markers"><span>released · 0</span><span>4095 · full scale</span></div>
        <div class="fs-meter-heading"><span id="fs-pressure-label">Normalized pressure</span><output id="fs-pressure-value">0 / 127</output></div><meter id="fs-pressure-meter" min="0" max="127" value="0">0</meter>
        <div id="fs-aftertouch-gauge" hidden><div class="fs-meter-heading"><span>Aftertouch</span><output id="fs-aftertouch-value">0 / 127</output></div><meter id="fs-aftertouch-meter" min="0" max="127" value="0">0</meter></div>
        <div class="fs-sensor-readout"><span>Raw <b id="fs-sensor-raw">0</b></span><span>Baseline <b id="fs-baseline">0</b></span><span>Gate <b id="fs-thresholds">—</b></span></div>
        <div class="fs-sensor-readout"><span>Last strike <b id="fs-last-strike">—</b></span><span>Recent rise <b id="fs-strike-rate">0 ADC/ms</b></span></div>
        <button class="button button-small fs-primary" id="fs-sample" type="button">Start sampling</button>
        <div class="fs-actions" id="fs-strike-auditions"><button class="button button-small" type="button" data-strike="soft">Soft strike</button><button class="button button-small" type="button" data-strike="medium">Medium strike</button><button class="button button-small" type="button" data-strike="firm">Firm strike</button></div>
        <p class="fs-hint">Auditions feed timed raw ADC ramps through the same C detector and velocity estimator. Browser scheduling is a simulation, not a hardware timing measurement.</p>
        <p class="fs-hint">100 Hz foreground target. Start qualifies a quiet input; stop sends real, timed release samples. Browser timing is not a hardware measurement.</p>
      </section>
      <section class="fs-section fs-wizard-section" aria-labelledby="fs-wizard-title">
        <div class="fs-sensor-heading"><h2 id="fs-wizard-title">Pad calibration</h2><span class="fs-sensor-state" id="fs-wizard-status">NOT STARTED</span></div>
        <p class="fs-copy" id="fs-wizard-instruction">Four phases per pad: Soft strikes, Mid strikes, Firm strikes, then a continuous Pressure hold. Three strikes at each level; hold softly for one second, increase to firm and release. Nothing changes until the final Apply.</p>
        <div class="fs-actions"><button class="button button-small fs-primary" id="fs-wizard-open" type="button">Start / resume</button>
          <button class="button button-small" id="fs-wizard-close" type="button">Close, keep measurements</button>
          <button class="button button-small" type="button" data-calibration-action="learn">Learn</button>
          <button class="button button-small" type="button" data-calibration-action="cancel">Cancel</button></div>
        <div class="fs-wizard-grids" id="fs-wizard-grids"></div>
        <div class="fs-wizard-gauge" id="fs-wizard-gauge" hidden><div class="fs-meter-heading"><span>Pressure hold · sensor range</span><output id="fs-wizard-gauge-value">0 ADC</output></div><meter id="fs-wizard-pressure-meter" min="0" max="4095" value="0">0</meter><p class="fs-hint" id="fs-wizard-hold-range">Keep contact: soft for one second, then firm, then release.</p></div>
        <p class="fs-scope-note" id="fs-wizard-feedback"></p>
        <section class="fs-wizard-review" id="fs-wizard-review" hidden><h3>Review the whole board</h3>
          <p class="fs-copy">All 48 pads in this cluster are measured. Play with the saved response or the calibrated response, then tune this half's sensitivity. The other half keeps its saved response. No settings change until Apply.</p>
          <label>Audition response<select id="fs-wizard-review-mode"><option value="saved">Saved</option><option value="calibrated">Calibrated</option></select></label>
          <label>Overall sensitivity · %<input id="fs-wizard-review-sensitivity" type="number" min="0" max="100" step="1"></label>
          <p class="fs-scope-note" id="fs-wizard-weak-pads"></p>
          <div class="fs-actions"><button class="button button-small fs-primary" type="button" data-calibration-action="apply">Apply calibration</button><button class="button button-small" type="button" data-calibration-action="discard">Discard calibration</button></div>
        </section>
        <p class="fs-hint">The OLED, pad order, gauge and fitted values come from C. Only the lit target contributes measurements. Use Sensor lab's timed strikes for the first three phases; for Pressure, start raw sampling, hold a low value for one second, raise it smoothly, then return to zero. Review opens after all 48 pads in the selected cluster. Learn also restarts a completed pad.</p>
      </section>
    </div>
    <div class="fs-footer"><div class="fs-profile-actions"><button id="fs-save" class="button button-small" type="button">Save scope locally</button><button id="fs-load" class="button button-small" type="button">Load saved scope</button><span>Browser profile · never written to the device</span></div><p class="fs-hint">Expression profiles are saved with full settings; this scope file contains response, velocity and lighting.</p><p id="fs-message" class="fs-message" role="status" aria-live="polite">Factory response is the starting point. Changes remain in this preview.</p></div>`;
  instrument.insertAdjacentElement('afterend', panel);
  if (!document.getElementById('firmware-studio-styles')) {
    const style = document.createElement('style');
    style.id = 'firmware-studio-styles';
    style.textContent = `
      .firmware-studio{margin-top:20px;overflow:hidden}.firmware-studio>summary{padding:18px 21px;display:flex;align-items:center;gap:18px;cursor:pointer;list-style:none}.firmware-studio>summary::-webkit-details-marker{display:none}.fs-summary-detail{font-size:10px;color:var(--faint);margin-left:auto}.fs-chevron{color:var(--muted);font-size:18px;transition:transform .2s}.firmware-studio[open] .fs-chevron{transform:rotate(180deg)}.fs-intro{display:flex;justify-content:space-between;align-items:center;padding:0 21px 17px;border-bottom:1px solid var(--line);gap:15px}.fs-intro p{font-size:12px;color:var(--muted);margin:0}.fs-local-tag{font:8px var(--mono);color:var(--accent);border:1px solid #66533d;border-radius:3px;padding:4px 6px;white-space:nowrap}.fs-columns{display:grid;grid-template-columns:1.1fr .85fr 1fr}.fs-section{padding:22px;min-width:0}.fs-section+.fs-section{border-left:1px solid var(--line)}.fs-section h2{font-size:15px;margin:0 0 9px}.fs-copy{color:var(--muted);font-size:11px;line-height:1.65;margin:0 0 16px;min-height:36px}.fs-fields{display:grid;grid-template-columns:1fr 1fr;gap:12px}.fs-full{grid-column:1/-1}.firmware-studio input[type=number]{display:block;background:#282c31;border:1px solid #3c4148;border-radius:5px;height:35px;margin-top:6px;color:var(--text);width:100%;padding:6px 9px;font:12px var(--mono)}.firmware-studio select{height:35px}.fs-scope-note{font:9px var(--mono);line-height:1.6;color:var(--accent);min-height:14px;margin:9px 0 15px}.fs-actions{display:flex;gap:8px;flex-wrap:wrap}.fs-primary{border-color:#766043;color:#edc58f;background:#e7b6730b}.fs-hint{font-size:10px;line-height:1.65;color:var(--faint);margin:12px 0 0}.fs-text-button{border:0;background:none;color:var(--muted);padding:0;margin-top:14px;text-align:left;font-size:10px;text-decoration:underline;text-underline-offset:3px}.fs-text-button:hover{color:var(--text)}.fs-curve-chart{display:block;width:100%;height:74px;margin:14px 0 16px;overflow:visible}.fs-curve-chart path{fill:none;stroke-width:1.5}.fs-chart-axis,.fs-chart-guide{stroke:#3b4148}.fs-chart-guide{stroke-dasharray:3 5}.fs-chart-line{stroke:var(--accent);stroke-width:2!important}.fs-chart-gates{stroke:#91bf9a70;stroke-dasharray:2 3}.fs-curve-chart text{font:8px var(--mono);fill:var(--faint)}.fs-switch{display:flex;align-items:center;gap:8px;margin:14px 0;color:#b7bec4;font-size:11px;cursor:pointer}.fs-switch input{accent-color:var(--accent);width:14px;height:14px;margin:0}.fs-range-label{margin-top:20px}.fs-range-label output{float:right;color:#c9c4b8;font:10px var(--mono)}.fs-expression{margin-top:24px;padding-top:17px;border-top:1px solid var(--line)}.fs-expression h3{font-size:11px;font-weight:500;color:#c0c5ca;margin:0}.fs-expression>label:last-of-type{margin-top:16px}.fs-sensor-heading{display:flex;justify-content:space-between;align-items:start;gap:8px}.fs-sensor-state{font:8px var(--mono);color:var(--muted);background:#2a3035;padding:4px 6px;border-radius:3px}.fs-sensor-state[data-state=held]{color:var(--green);background:#91bf9a15}.fs-sensor-state[data-state=blocked]{color:var(--red)}.fs-raw-heading{display:flex;align-items:center;justify-content:space-between;margin-top:24px}.fs-raw-heading output{font:24px var(--mono);color:var(--text)}.fs-raw-markers{display:flex;justify-content:space-between;font:8px var(--mono);color:var(--faint);margin-top:5px}.fs-meter-heading{margin-top:22px;display:flex;justify-content:space-between;font-size:10px;color:var(--muted)}.fs-meter-heading output{font:10px var(--mono);color:var(--accent)}#fs-pressure-meter{width:100%;height:9px;margin-top:9px;accent-color:var(--accent)}#fs-pressure-meter::-webkit-meter-bar{background:#30353b;border:0;border-radius:2px}#fs-pressure-meter::-webkit-meter-optimum-value{background:var(--accent);border-radius:2px}.fs-sensor-readout{display:flex;gap:10px;flex-wrap:wrap;font:8px var(--mono);color:var(--faint);margin:15px 0 20px}.fs-sensor-readout b{color:#bcc3c7;font-weight:400;margin-left:3px}.fs-footer{padding:16px 21px;border-top:1px solid var(--line);background:#181c20}.fs-profile-actions{display:flex;align-items:center;gap:8px;flex-wrap:wrap}.fs-profile-actions>span{color:var(--faint);font-size:9px;margin-left:4px}.fs-message{font-size:11px;color:var(--muted);line-height:1.6;margin:13px 0 0}.fs-message[data-error=true]{color:var(--red)}.firmware-studio button:disabled{opacity:.4}.firmware-studio input:disabled{opacity:.4}.firmware-studio summary:focus-visible{outline:2px solid var(--accent);outline-offset:-3px}@media(max-width:1100px){.fs-columns{grid-template-columns:1fr 1fr}.fs-sensor-section{grid-column:1/-1;border-left:0!important;border-top:1px solid var(--line);display:block}.fs-sensor-section .fs-copy{min-height:0}.fs-sensor-section input[type=number]{max-width:180px}}@media(max-width:660px){.fs-columns{grid-template-columns:1fr}.fs-section+.fs-section{border-left:0;border-top:1px solid var(--line)}.fs-section{padding:19px}.fs-summary-detail{display:none}.firmware-studio>summary,.fs-intro,.fs-footer{padding-left:19px;padding-right:19px}.fs-intro{align-items:start}.fs-local-tag{font-size:7px}.fs-profile-actions>span{width:100%;margin:5px 0 0}}`;
    style.textContent+=`.firmware-studio [hidden]{display:none!important}.fs-wizard-section{grid-column:1/-1;border-left:0!important;border-top:1px solid var(--line)}.fs-wizard-section .fs-copy{min-height:0}.fs-wizard-grids{display:grid;grid-template-columns:1fr 1fr;gap:24px;margin-top:18px}.fs-wizard-grid{display:grid;grid-template-columns:repeat(12,minmax(12px,1fr));gap:4px}.fs-wizard-grid button{aspect-ratio:1;border:1px solid #3c4148;border-radius:3px;background:#292f34;color:var(--faint);font:8px var(--mono);padding:0}.fs-wizard-grid button[data-completed=true]{background:#514123;color:#edc58f}.fs-wizard-grid button[data-target=true]{outline:2px solid var(--accent)}.fs-wizard-grid button[aria-pressed=true]{border-color:#e6e7e8}.fs-wizard-grid button[data-outlier=true]{box-shadow:inset 0 0 0 2px #cf9a63;color:#f3c189}.fs-wizard-review{margin-top:20px;border-top:1px solid var(--line);padding-top:18px;max-width:620px}.fs-wizard-review h3{margin:0 0 10px;font-size:14px}.fs-wizard-gauge{max-width:500px}.fs-wizard-gauge[hidden]{display:none}#fs-wizard-pressure-meter{width:100%;height:12px;margin-top:8px;accent-color:var(--accent)}.fs-wizard-compare{display:grid;grid-template-columns:2fr 1fr 1fr;max-width:440px;gap:8px;font:11px var(--mono);color:var(--muted);margin-top:12px}.fs-wizard-compare output{color:var(--text)}@media(max-width:660px){.fs-wizard-grids{grid-template-columns:1fr}}`;
    style.textContent += `.fs-profile-at-line{stroke:var(--green,#91bf9a);stroke-width:2!important}#fs-aftertouch-meter{width:100%;height:9px;margin-top:9px;accent-color:var(--green,#91bf9a)}#fs-aftertouch-meter::-webkit-meter-bar{background:#30353b;border:0;border-radius:2px}#fs-aftertouch-meter::-webkit-meter-optimum-value{background:var(--green,#91bf9a);border-radius:2px}.fs-dual-profile .fs-fields{margin-bottom:14px}`;
    document.head.append(style);
  }

  const $ = id => panel.querySelector(`#fs-${id}`);
  const responseInputs = RESPONSE_KEYS.map(key => $(key));
  const velocityInputs = VELOCITY_KEYS.map(key => $('strike-'+key));
  const profileInputs=['enabled','split','pressureCurve','aftertouchCurve'].map(key=>$('profile-'+key));
  let profileDirty=false,previousProfileScope='',previousProfile='',previousProfileGraph='';
  let responseDirty = false;
  let velocityDirty = false;
  let previousScope = '';
  let previousResponse = '';
  let previousVelocityScope = '';
  let previousVelocity = '';
  let amountDirty=false,policyDirty=false,previousPolicy='',previousAmount='';
  let disposed = false;
  let phase = 'idle';
  let labPad = 0;
  let timer;
  let quietSamples = 0;
  let ready = false;
  let audition = null;
  let auditionResult = null;
  const clock = () => Math.floor(performance.now()) >>> 0;
  const selected = () => {
    const value = getContext()?.selectedPad;
    return Number.isInteger(value) && value >= 0 && value < 96 ? value : 0;
  };
  const target = () => $('scope').value === 'left' ? -1 : $('scope').value === 'right' ? -2 : selected();
  const scopeName = pad => pad===-2?'Right cluster':pad<0?'Left cluster':`Pad ${pad}`;
  const message = (text, isError = false) => {
    $('message').textContent = text;
    $('message').dataset.error = String(isError);
  };
  const changed = () => { onEvents(); onChange(); refresh(); };
  const responseDraft = () => Object.fromEntries(RESPONSE_KEYS.map(key => [key, Number($(key).value)]));
  const velocityDraft = () => Object.fromEntries(VELOCITY_KEYS.map(key => [key, Number($('strike-'+key).value)]));
  const profileDraft = () => ({enabled:$('profile-enabled').checked,split:Number($('profile-split').value),
    pressureCurve:Number($('profile-pressureCurve').value),aftertouchCurve:Number($('profile-aftertouchCurve').value)});
  const location = pad => `${pad < 48 ? 'LEFT' : 'RIGHT'} · column ${Math.floor((pad % 48) / 4) + 1}, row ${pad % 4 + 1} · ADC ${pad}`;
  const responseFailure = () => {
    message('Release the affected pads and stop sensor sampling before changing response. The requested change was not applied.', true);
    refresh();
  };
  const busy = () => phase !== 'idle';
  for(let side=0;side<2;side++) {
    const grid=document.createElement('div');grid.className='fs-wizard-grid';grid.setAttribute('aria-label',side?'Right calibration pads':'Left calibration pads');
    for(let row=0;row<4;row++)for(let column=0;column<12;column++) {
      const pad=side*48+column*4+row,button=document.createElement('button');button.type='button';
      button.dataset.calibrationPad=pad;button.textContent=column+1;button.title=`${side?'Right':'Left'} row ${row+1}, column ${column+1}`;
      button.addEventListener('click',()=>{
        if(busy())return;
        if(!engine.calibration.select(pad).ok){message('Release the current contact before selecting a pad.',true);return;}
        selectPad(pad);changed();
      });grid.append(button);
    }
    $('wizard-grids').append(grid);
  }

  function refreshWizard() {
    const w=engine.calibration.snapshot(),stage=['SOFT','MID','FIRM','PRESSURE'][w.stage]||'SOFT';
    const states=['NOT STARTED','LEARNING','REVIEW','SAVED AUDITION','CALIBRATED AUDITION','APPLYING','APPLIED','CANCELLED'];
    const review=w.scopeCount>0&&w.completed===w.scopeCount&&Boolean(w.reviewReady)&&[2,3,4,5,6].includes(w.state);
    const cluster=w.scopeFirst===48?'RIGHT':'LEFT';
    $('wizard-status').textContent=`${cluster} ${states[w.state]||'UNKNOWN'} · ${w.completed}/${w.scopeCount||48}`;
    $('wizard-open').disabled=busy()||w.open;$('wizard-close').disabled=!w.open;
    const candidate=engine.calibration.candidate(w.selected);
    panel.querySelectorAll('[data-calibration-action]').forEach(button=>{
      const action=button.dataset.calibrationAction;
      button.hidden=review?['learn','cancel'].includes(action):['apply','discard'].includes(action);
      button.disabled=!w.open||busy()||(['apply','discard'].includes(action)&&!review);
    });
    panel.querySelectorAll('[data-calibration-pad]').forEach(button=>{
      const pad=Number(button.dataset.calibrationPad);button.disabled=!w.open||busy()||pad<w.scopeFirst||pad>=w.scopeFirst+w.scopeCount;
      button.dataset.completed=engine.calibration.completed(pad);button.dataset.target=w.state===1&&w.target===pad;
      const strength=review&&pad>=w.scopeFirst&&pad<w.scopeFirst+w.scopeCount?engine.calibration.strength(pad):null;
      button.dataset.outlier=Boolean(strength?.flagged);
      button.title=`${pad<48?'Left':'Right'} row ${pad%4+1}, column ${Math.floor((pad%48)/4)+1}${strength?` · ${strength.percent}% of board median${strength.flagged?' · check response':''}`:''}`;
      button.setAttribute('aria-pressed',String(w.selected===pad));
    });
    $('wizard-instruction').textContent=w.open&&w.state===1?
      `${location(w.target).split(' · ADC')[0]} · ${w.stage+1}/4 ${stage}: ${w.stage===3?'keep contact softly for one second, increase to firm, then release.':`strike ${Math.min(3,w.hits+1)}/3, then release fully.`} Learning stays silent.`:
      review?`${cluster} cluster audition: switch Saved / Calibrated and play any pad. Tune this half's sensitivity, then Apply or Discard. Saved settings remain unchanged during review.`:
      w.open&&w.state===6?'Changes applied. Close calibration to let local settings saving finish.':
      'Four phases per pad: three Soft strikes, three Mid strikes, three Firm strikes, then a continuous soft-to-firm Pressure hold. Nothing changes until the final Apply.';
    $('wizard-feedback').textContent=w.message|| (w.pick===1?'Touch the pad to learn. That selection touch is not a calibration sample.':
      w.pick===2?'Release the selected pad; its next strike starts learning.':
      candidate?`Measured: aftertouch ${candidate.floor}–${candidate.ceiling}; strike ${candidate.softRate}–${candidate.hardRate} ADC/ms.`:
      'Measurements are temporary until the final Apply. Closing keeps them for this session; Cancel or a restart discards unsaved work.');
    $('wizard-gauge').hidden=!w.open||w.state!==1||w.stage!==3;
    const gauge=Math.max(0,Math.min(4095,w.raw));
    $('wizard-pressure-meter').value=gauge;$('wizard-gauge-value').textContent=`${gauge} ADC`;
    $('wizard-hold-range').textContent=w.holdSoft>0?`Soft anchor ${w.holdSoft} ADC · firm peak ${Math.max(w.holdSoft,w.holdFirm)} ADC. Keep contact until the firm hold is complete, then release.`:
      'Keep contact: soft for one second, then firm, then release. This phase measures sustained pressure, not strike speed.';
    $('wizard-review').hidden=!review;
    $('wizard-review-mode').disabled=!review||busy();
    $('wizard-review-sensitivity').disabled=!review||busy();
    if(document.activeElement!==$('wizard-review-mode'))$('wizard-review-mode').value=w.state===3?'saved':'calibrated';
    if(document.activeElement!==$('wizard-review-sensitivity'))$('wizard-review-sensitivity').value=w.previewPercent;
    const strength=review?engine.calibration.strength(w.selected):null;
    $('wizard-weak-pads').textContent=review?`${w.outlierCount} response outliers; ${w.leakOutlierCount} coupled pads keep their saved response unchanged.${strength?` Selected pad: ${strength.percent}% of cluster median.`:''} These are relative gesture measurements, not a diagnosis of shims or hardware. Check marked pads with the same playing gesture.`:'';
  }

  function renderCurve() {
    const draft = responseDraft();
    if (validateFirmwareResponse(draft)) {
      $('curve-line').setAttribute('d', '');
      $('gate-lines').setAttribute('d', '');
      return;
    }
    const points = Array.from({ length: 101 }, (_, i) => {
      const raw = Math.round(i * 4095 / 100);
      return `${i ? 'L' : 'M'}${1 + i * 2.98},${75 - previewPressure(draft, raw) * 74 / 127}`;
    });
    $('curve-line').setAttribute('d', points.join(' '));
    $('gate-lines').setAttribute('d', [draft.release, draft.attack].map(raw => {
      const x = 1 + raw * 298 / 4095;
      return `M${x} 1V75`;
    }).join(' '));
  }

  function renderVelocityCurve() {
    const draft = velocityDraft();
    if (validateFirmwareVelocity(draft)) { $('strike-line').setAttribute('d',''); return; }
    const limit = Math.min(4095,Math.max(1,Math.ceil(draft.hardRate*1.1)));
    const fixed = firmware.routeForPad(target()===-2?48:target()<0?0:selected()).velocity;
    const points = Array.from({length:101},(_,index)=>{
      const rate = Math.round(index*limit/100), velocity = draft.mode===0?fixed:firmware.velocityValue(draft,rate);
      return `${index?'L':'M'}${1+index*2.98},${75-velocity*74/127}`;
    });
    $('strike-line').setAttribute('d',points.join(' '));
    $('strike-axis').textContent = `${limit} ADC/ms`;
  }

  function renderExpressionProfileCurve() {
    const samples=sampleFirmwareExpressionProfile(firmware,target());
    const path=key=>samples.map((sample,index)=>`${index?'L':'M'}${1+index*298/(samples.length-1)},${75-sample[key]*74/127}`).join(' ');
    $('profile-pressure-line').setAttribute('d',path('pressure'));
    $('profile-at-line').setAttribute('d',firmware.expressionProfile(target()).enabled?path('aftertouch'):'');
  }

  function refresh() {
    if (disposed) return;
    const state = firmware.state();
    const pad = busy() ? labPad : selected();
    const sensor = firmware.sensor(pad);
    const scope = `${$('scope').value}:${selected()}`;
    const response = firmware.response(target());
    const profile=firmware.expressionProfile(target());
    const profileSignature=JSON.stringify(profile);
    if(scope!==previousProfileScope || !profileDirty && profileSignature!==previousProfile) {
      $('profile-enabled').checked=profile.enabled;
      for(const key of ['split','pressureCurve','aftertouchCurve'])$('profile-'+key).value=profile[key];
      profileDirty=false;previousProfileScope=scope;previousProfile=profileSignature;
    }
    const review=engine.calibration.snapshot();
    const profileGraphSignature=JSON.stringify([scope,profile,response,review.state,review.previewPercent,review.selected]);
    if(profileGraphSignature!==previousProfileGraph) {
      previousProfileGraph=profileGraphSignature;renderExpressionProfileCurve();
    }
    $('profile-scope').textContent=target()<0?`${scopeName(target())} profile · individual overrides stay unchanged`:
      `${location(selected())} · ${profile.overridden?'individual expression profile':'inheriting this half’s expression profile'}`;
    $('profile-inherit').hidden=target()<0;
    $('profile-inherit').disabled=busy()||!profile.overridden;
    $('profile-apply').disabled=busy();profileInputs.forEach(input=>{input.disabled=busy();});
    const velocity = firmware.velocity(target());
    const policy=firmware.releasePolicy(target()),bounds=firmware.sensitivityBounds(target());
    const amountSignature=JSON.stringify([scope,firmware.sensitivity(target()),bounds]);
    if(scope!==previousScope||!amountDirty&&amountSignature!==previousAmount) {
      $('amount').value=firmware.sensitivity(target());amountDirty=false;previousAmount=amountSignature;
    }
    if(bounds){$('amount').min=bounds.min;$('amount').max=bounds.max;}
    const policySignature=JSON.stringify([scope,policy]);
    if(scope!==previousScope||!policyDirty&&policySignature!==previousPolicy) {
      $('linked-release').checked=Boolean(policy.linked);$('release-gap').value=policy.gap;
      policyDirty=false;previousPolicy=policySignature;
    }
    const signature = JSON.stringify(response);
    if (scope !== previousScope || (!responseDirty && signature !== previousResponse)) {
      RESPONSE_KEYS.forEach(key => { $(key).value = response[key]; });
      responseDirty = false;
      previousScope = scope;
      previousResponse = signature;
      renderCurve();
    }
    const velocitySignature = JSON.stringify([velocity,firmware.routeForPad(target()===-2?48:target()<0?0:selected()).velocity]);
    if (scope !== previousVelocityScope || (!velocityDirty && velocitySignature !== previousVelocity)) {
      VELOCITY_KEYS.forEach(key=>{ $('strike-'+key).value=velocity[key]; });
      velocityDirty=false; previousVelocityScope=scope; previousVelocity=velocitySignature;
      renderVelocityCurve();
    }
    $('strike-scope').textContent = target()<0 ? `${scopeName(target())} velocity · individual overrides stay unchanged` :
      `${location(selected())} · ${velocity.overridden?'individual velocity override':'inheriting this half’s velocity'}`;
    $('strike-inherit').hidden=target()<0;
    $('strike-inherit').disabled=busy()||!velocity.overridden;
    $('strike-apply').disabled=busy(); $('strike-reset').disabled=busy();
    velocityInputs.forEach(input=>{input.disabled=busy();});
    panel.querySelectorAll('[data-strike]').forEach(button=>{button.disabled=busy();});
    $('scope-note').textContent = target() < 0 ? `${scopeName(target())} response · individual overrides keep their settings` :
      `${location(selected())} · ${response.overridden ? 'individual override' : 'inheriting this half’s response'}`;
    $('inherit').hidden = target() < 0;
    $('inherit').disabled = busy() || !response.overridden;
    $('apply').disabled = busy();
    $('scope').disabled = busy();
    for(const id of ['amount','amount-apply','linked-release','release-gap','release-apply'])$(id).disabled=busy();
    $('load').disabled = busy();
    responseInputs.forEach(input => { input.disabled = busy(); });
    $('light-enabled').checked = Boolean(state.lightsEnabled);
    $('idle').value = state.lightIdle;
    $('held').value = state.lightHeld;
    $('fade').value = state.lightFade;
    $('idle-value').textContent = `${state.lightIdle} / 8`;
    $('held-value').textContent = `${state.lightHeld} / 8`;
    $('fade-value').textContent = `${state.lightFade} ms`;
    $('pressure-enabled').checked = Boolean(state.pressureEnabled);
    if (document.activeElement !== $('velocity')) $('velocity').value = state.velocity;
    $('pad').value = pad;
    $('pad').disabled = busy();
    $('pad-location').textContent = location(pad);
    $('raw-value').textContent = $('raw').value;
    $('raw').disabled = phase !== 'sampling' || !ready;
    const sensorProfile=firmware.expressionProfile(pad);
    const axes=sensorProfile.enabled?firmware.expressionAxes(pad,sensor.raw):{pressure:sensor.pressure,aftertouch:0};
    $('pressure-label').textContent=sensorProfile.enabled?'Primary pressure':'Normalized pressure';
    $('pressure-value').textContent = `${axes.pressure} / 127`;
    $('pressure-meter').value = axes.pressure;
    $('aftertouch-gauge').hidden=!sensorProfile.enabled;
    $('aftertouch-value').textContent=`${axes.aftertouch} / 127`;
    $('aftertouch-meter').value=axes.aftertouch;
    $('sensor-raw').textContent = sensor.raw;
    $('baseline').textContent = sensor.baseline;
    $('thresholds').textContent = `${sensor.attack} / ${sensor.release}`;
    $('last-strike').textContent = sensor.lastStrike ? `${sensor.lastStrike} / 127` : '—';
    $('strike-rate').textContent = `${sensor.strikeRate} ADC/ms`;
    const status = phase === 'releasing' ? 'RELEASING' : sensor.blocked ? 'BLOCKED' : sensor.held ? 'HELD' :
      phase === 'audition' ? ready ? 'STRIKE TEST' : `QUIET ${sensor.warmup}/8` :
      phase === 'sampling' && !ready ? `QUIET ${sensor.warmup}/8` : phase === 'sampling' ? 'SAMPLING' : 'IDLE';
    $('sensor-state').textContent = status;
    $('sensor-state').dataset.state = sensor.blocked ? 'blocked' : sensor.held ? 'held' : 'idle';
    $('sample').textContent = phase === 'releasing' ? 'Releasing…' : phase === 'sampling' ? 'Stop & release' : 'Start sampling';
    $('sample').disabled = phase === 'releasing' || phase === 'audition';
    refreshWizard();
  }

  function tick() {
    timer = undefined;
    if (disposed || phase === 'idle') return;
    const now = clock();
    const raw = phase === 'releasing' || !ready ? 0 : phase === 'audition' ?
      Math.min(4095,Math.floor(audition.rate*((now-audition.started)>>>0))) : Number($('raw').value);
    $('raw').value=raw;
    let result = firmware.sample(labPad, raw, now);
    if(result.ok)result=engine.advance(now);
    if (!result.ok) {
      engine.panic();
      phase = 'idle';
      $('raw').value = 0;
      changed();
      message('The sample was rejected. Notes were released; restart the lab with a quiet input.', true);
      return;
    }
    if (phase === 'releasing') {
      ++quietSamples;
      if (quietSamples >= 8 && !firmware.sensor(labPad).held) {
        phase = 'idle';
        ready = false;
        changed();
        message(auditionResult ? `Strike audition: velocity ${auditionResult.velocity}, recent rise ${auditionResult.rate} ADC/ms. The pad is released. Browser timing is simulated.` : 'Sampling stopped. The pad received eight timed quiet samples and is released.');
        audition=null;
        return;
      }
    } else if (!ready) {
      ++quietSamples;
      if (quietSamples >= 8 && firmware.sensor(labPad).warmup >= 8) {
        ready = true;
        if(audition) audition.started=now;
      }
    } else if (phase === 'audition') {
      const current=firmware.sensor(labPad);
      if(current.held) {
        auditionResult={velocity:current.lastStrike,rate:current.strikeRate};
        phase='releasing'; quietSamples=0;
      } else if(((now-audition.started)>>>0)>5000) {
        phase='releasing';quietSamples=0;
        message('This raw ramp did not qualify a strike. Releasing the input; check its response thresholds.',true);
      }
    }
    changed();
    timer = window.setTimeout(tick, 10);
  }

  function stopSampling({ panic = false } = {}) {
    if (phase === 'idle') return;
    phase = 'releasing';
    audition=null;
    quietSamples = 0;
    $('raw').value = 0;
    if (panic) { engine.panic(); onEvents(); onChange(); }
    if (timer !== undefined) window.clearTimeout(timer);
    timer = window.setTimeout(tick, 10);
    refresh();
  }

  $('sample').addEventListener('click', () => {
    if (phase === 'sampling') { stopSampling(); return; }
    if (busy()) return;
    labPad = selected();
    if (engine.isHeld(labPad)) {
      message('Release this on-screen pad before starting sensor simulation.', true);
      return;
    }
    phase = 'sampling';
    audition=null; auditionResult=null;
    ready = false;
    quietSamples = 0;
    $('raw').value = 0;
    message('Sampling the shared C detector. Move the raw ADC slider after quiet qualification.');
    tick();
  });
  panel.querySelectorAll('[data-strike]').forEach(button=>button.addEventListener('click',()=>{
    if(busy())return;
    labPad=selected();
    if(engine.isHeld(labPad)||firmware.sensor(labPad).held) {
      message('Release this pad before starting a strike audition.',true);return;
    }
    const config=firmware.velocity(labPad);
    const rate=button.dataset.strike==='soft'?Math.max(1,config.softRate):
      button.dataset.strike==='firm'?config.hardRate:Math.max(1,Math.floor((config.softRate+config.hardRate)/2));
    audition={rate,started:0}; auditionResult=null;
    phase='audition';ready=false;quietSamples=0;$('raw').value=0;
    message(config.mode===0?'Auditioning raw input in Fixed mode. Select Strike to hear velocity differences.':'Auditioning a raw ADC rise through the shared C strike estimator.');
    tick();
  }));
  $('raw').addEventListener('input', () => { $('raw-value').textContent = $('raw').value; });
  $('pad').addEventListener('change', () => {
    const pad = Number($('pad').value);
    if (Number.isInteger(pad) && pad >= 0 && pad < 96) { selectPad(pad); responseDirty = velocityDirty = profileDirty = false; refresh(); }
    else { message('Choose a whole electrical pad ID from 0 to 95.', true); refresh(); }
  });
  $('scope').addEventListener('change', () => { responseDirty = velocityDirty = amountDirty = policyDirty = profileDirty = false; refresh(); });
  $('wizard-open').addEventListener('click',()=>{
    if(busy())return;
    const scope=target();
    const targetRow=MENU_ROWS.findIndex(row=>row.id==='sensitivity.target');
    const focused=engine.state.set(targetRow,scope===-2?97:scope===-1?0:scope+1);
    if(!focused.ok){message('Release all pads before choosing a calibration cluster.',true);return;}
    const opened=engine.calibration.open();
    if(!opened.ok){message('Stop playback and release every pad before opening calibration.',true);return;}
    message('Calibration opened. Raw input is muted while C collects the measurements.');changed();
  });
  $('wizard-close').addEventListener('click',()=>{
    if(busy())stopSampling();const result=engine.calibration.close();
    message(result.ok?'Calibration closed; unsaved measurements remain for this session.':'Release all sounding pads before closing calibration.',!result.ok);changed();
  });
  panel.querySelectorAll('[data-calibration-action]').forEach(button=>button.addEventListener('click',()=>{
    if(busy())return;
    const action=button.dataset.calibrationAction,result=engine.calibration.action(action);
    message(result.ok?(action==='apply'?'Complete calibration applied through C. Normal local saving follows when the instrument is idle.':
      ['cancel','discard'].includes(action)?'Unsaved calibration discarded. Saved settings were not changed.':
      'Touch and release the pad to learn. A completed pad is restarted; the selection touch is not a sample.'):
      'This action is not ready. Release all pads, or finish the measurements before Apply.',!result.ok);
    changed();
  }));
  $('wizard-review-mode').addEventListener('change',()=>{
    if(busy())return;
    const result=engine.calibration.action($('wizard-review-mode').value==='saved'?'before':'after');
    message(result.ok?'Selected-cluster audition changed. Saved settings are unchanged.':'Release all pads before changing audition response.',!result.ok);changed();
  });
  $('wizard-review-sensitivity').addEventListener('change',()=>{
    if(busy())return;
    const value=Number($('wizard-review-sensitivity').value);
    const result=engine.calibration.setReviewSensitivity(value);
    message(result.ok?'This half’s sensitivity changed for calibrated audition only. Apply retains it.':'Use a whole percentage from 0 to 100, and release all pads before tuning.',!result.ok);changed();
  });
  $('amount').addEventListener('input',()=>{amountDirty=true;});
  $('amount').addEventListener('change',()=>{
    const value=Number($('amount').value),bounds=firmware.sensitivityBounds(target());
    if($('amount').value!==''&&Number.isInteger(value)&&bounds)$('amount').value=Math.max(bounds.min,Math.min(bounds.max,value));
  });
  $('sensitivity-form').addEventListener('submit',event=>{
    event.preventDefault();if(busy())return;
    const amount=Number($('amount').value),bounds=firmware.sensitivityBounds(target());
    if(!Number.isInteger(amount)||!bounds){message('Choose a whole Sensitivity value.',true);return;}
    const clamped=Math.max(bounds.min,Math.min(bounds.max,amount));$('amount').value=clamped;
    if(!firmware.setSensitivityAmount(target(),clamped).ok){responseFailure();return;}
    amountDirty=responseDirty=policyDirty=false;message('Sensitivity applied through the shared C setting.');changed();
  });
  for(const id of ['linked-release','release-gap'])$(id).addEventListener('input',()=>{policyDirty=true;});
  $('release-form').addEventListener('submit',event=>{
    event.preventDefault();if(busy())return;
    const policy={linked:Number($('linked-release').checked),gap:Number($('release-gap').value)},invalid=validateFirmwareReleasePolicy(policy);
    if(invalid){message(invalid,true);return;}
    if(!firmware.setReleasePolicy(target(),policy).ok){responseFailure();return;}
    policyDirty=responseDirty=false;message('Release policy applied; aftertouch settings are unchanged.');changed();
  });
  responseInputs.forEach(input => input.addEventListener('input', () => { responseDirty = true; renderCurve(); }));
  $('response-form').addEventListener('submit', event => {
    event.preventDefault();
    if (busy()) return;
    const response = responseDraft();
    const invalid = validateFirmwareResponse(response);
    if (invalid) { message(invalid, true); return; }
    if (!firmware.setResponse(target(), response).ok) { responseFailure(); return; }
    responseDirty = false;
    message(target() < 0 ? `${scopeName(target())} response applied to inheriting pads. Individual overrides are preserved.` : `Response applied to electrical pad ${selected()}.`);
    changed();
  });
  $('inherit').addEventListener('click', () => {
    if (busy()) return;
    if (!firmware.clearResponse(selected()).ok) { responseFailure(); return; }
    responseDirty = amountDirty = policyDirty = false;
    message(`Electrical pad ${selected()} now inherits its half's response.`);
    changed();
  });
  profileInputs.forEach(input=>input.addEventListener('input',()=>{profileDirty=true;}));
  $('profile-form').addEventListener('submit',event=>{
    event.preventDefault();if(busy())return;
    const profile=profileDraft(),invalid=validateFirmwareExpressionProfile(profile);
    if(invalid){message(invalid,true);return;}
    const applied=firmware.setExpressionProfile(target(),profile);
    if(!applied.ok){message('Release the affected pads before changing their expression profile. Nothing was applied.',true);return;}
    profileDirty=false;
    message(target()<0?`${scopeName(target())} expression profile applied; individual profiles are preserved.`:
      `Expression profile applied to electrical pad ${selected()}.`);
    changed();
  });
  $('profile-inherit').addEventListener('click',()=>{
    if(busy()||target()<0)return;
    if(!firmware.clearExpressionProfile(selected()).ok){message('Release this pad before restoring its cluster profile.',true);return;}
    profileDirty=false;message(`Electrical pad ${selected()} now inherits its cluster expression profile. Detector and velocity settings are unchanged.`);changed();
  });
  velocityInputs.forEach(input=>input.addEventListener('input',()=>{velocityDirty=true;renderVelocityCurve();}));
  $('strike-form').addEventListener('submit',event=>{
    event.preventDefault();if(busy())return;
    const config=velocityDraft(),invalid=validateFirmwareVelocity(config);
    if(invalid){message(invalid,true);return;}
    if(!firmware.setVelocity(target(),config).ok){responseFailure();return;}
    velocityDirty=false;
    message(target()<0?`${scopeName(target())} strike response applied; velocity overrides are preserved.`:`Strike response applied to electrical pad ${selected()}.`);
    changed();
  });
  $('strike-inherit').addEventListener('click',()=>{
    if(busy())return;
    if(!firmware.clearVelocity(selected()).ok){responseFailure();return;}
    velocityDirty=false;message(`Electrical pad ${selected()} now inherits its half's velocity. Pressure settings are unchanged.`);changed();
  });
  $('strike-reset').addEventListener('click',()=>{
    if(busy())return;
    if(!firmware.resetVelocity(target()).ok){responseFailure();return;}
    velocityDirty=false;message('Fixed velocity defaults restored for this scope. Pressure settings and other velocity overrides are unchanged.');changed();
  });
  function lightingChanged() {
    const result = firmware.lighting({ enabled: $('light-enabled').checked, idle: Number($('idle').value), held: Number($('held').value), fade: Number($('fade').value) });
    if (!result.ok) message('Choose brightness 0–8 and a release fade from 0–500 ms.', true);
    changed();
  }
  ['idle', 'held', 'fade'].forEach(id => $(id).addEventListener('input', lightingChanged));
  $('light-enabled').addEventListener('change', lightingChanged);
  function expressionChanged() {
    const result = firmware.expression(Number($('velocity').value), $('pressure-enabled').checked);
    if (!result.ok) message('Choose a whole fixed velocity from 1 to 127.', true);
    changed();
  }
  $('velocity').addEventListener('change', expressionChanged);
  $('pressure-enabled').addEventListener('change', expressionChanged);

  function exportSettings() {
    const state = firmware.state();
    const response = firmware.response(target());
    const velocity=firmware.velocity(target());
    const policy=firmware.releasePolicy(target());
    return { format: 'thy333.browser-response-scope', version: 4, pad: target(),
      response: Object.fromEntries(RESPONSE_KEYS.map(key => [key, response[key]])),
      velocity: Object.fromEntries(VELOCITY_KEYS.map(key=>[key,velocity[key]])),
      releasePolicy:{linked:policy.linked,gap:policy.gap},
      expression: { velocity: state.velocity, pressure: Boolean(state.pressureEnabled) },
      lighting: { enabled: Boolean(state.lightsEnabled), idle: state.lightIdle, held: state.lightHeld, fade: state.lightFade },
      note: 'Personal pressure/strike preview settings. Raw strike auditions use the shared C estimator; browser timing is not hardware calibration.' };
  }
  $('save').addEventListener('click', () => {
    try {
      const profile = exportSettings();
      localStorage.setItem(FIRMWARE_PROFILE_KEY, JSON.stringify(profile));
      message(`${scopeName(profile.pad)} response and expression/lighting saved in this browser. Draft edits are not saved.`);
    } catch { message('This browser could not save the profile. The current preview settings remain applied.', true); }
  });
  $('load').addEventListener('click', () => {
    if (busy()) return;
    let profile;
    try {
      const stored = localStorage.getItem(FIRMWARE_PROFILE_KEY);
      if (!stored) { message('No local scope profile has been saved yet.'); return; }
      profile = JSON.parse(stored);
    } catch { message('The saved browser profile could not be read.', true); return; }
    const expression = profile?.expression;
    const lighting = profile?.lighting;
    const integer = (value, min, max) => Number.isInteger(value) && value >= min && value <= max;
    if(profile?.format==='thy333.browser-response-scope'&&[1,2,3].includes(profile.version)&&profile.pad<0){
      message('This legacy profile targets the whole board. Save a new Left or Right cluster profile instead; nothing was loaded.',true);return;
    }
    if (profile?.format !== 'thy333.browser-response-scope' || ![1,2,3,4].includes(profile.version) || !integer(profile.pad, profile.version===4?-2:0, 95) ||
        validateFirmwareResponse(profile.response) || !integer(expression?.velocity, 1, 127) || typeof expression?.pressure !== 'boolean' ||
        profile.version>=2 && validateFirmwareVelocity(profile.velocity) ||
        profile.version>=3 && validateFirmwareReleasePolicy(profile.releasePolicy) ||
        typeof lighting?.enabled !== 'boolean' || !integer(lighting.idle, 0, 8) || !integer(lighting.held, 0, 8) || !integer(lighting.fade, 0, 500)) {
      message('The saved profile is invalid or uses an unsupported format. Nothing was loaded.', true);
      return;
    }
    const applied = profile.version>=2 ? firmware.setSensitivity(profile.pad,profile.response,profile.velocity,
      profile.version>=3?profile.releasePolicy:undefined) :
      firmware.setResponse(profile.pad,profile.response);
    if (!applied.ok) { responseFailure(); return; }
    firmware.expression(expression.velocity, expression.pressure);
    firmware.lighting(lighting);
    $('scope').value = profile.pad===-2?'right':profile.pad<0?'left':'pad';
    if (profile.pad >= 0) selectPad(profile.pad);
    responseDirty = velocityDirty = amountDirty = policyDirty = profileDirty = false;
    message(`${scopeName(profile.pad)} response loaded, with expression and lighting. Other pad overrides are preserved.`);
    changed();
  });
  const closeWizard=()=>{if(engine.calibration.snapshot().open){engine.calibration.close();onChange();refresh();}};
  const visibility = () => { if (document.hidden) {stopSampling({ panic: true });closeWizard();} };
  const blur = () => {stopSampling({ panic: true });closeWizard();};
  document.addEventListener('visibilitychange', visibility);
  window.addEventListener('blur', blur);
  panel.addEventListener('toggle', () => { if (!panel.open) {stopSampling();closeWizard();} });
  refresh();
  return {
    refresh,
    exportSettings,
    isSampling: pad => !disposed && busy() && (pad === undefined || pad === labPad),
    close() {
      if (disposed) return;
      closeWizard();
      if (timer !== undefined) window.clearTimeout(timer);
      if (busy()) {
        firmware.sample(labPad, 0, clock());
        engine.panic();
        onEvents();
      }
      disposed = true;
      phase = 'idle';
      document.removeEventListener('visibilitychange', visibility);
      window.removeEventListener('blur', blur);
      panel.remove();
    },
  };
}
