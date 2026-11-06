'use strict';

const $$ = (selector) => document.querySelector(selector);
const led = $$('#release-led');
const releaseStatus = $$('#release-status');
const installer = $$('#web-install');
const placeholder = $$('#installer-loading');
const modeFull = $$('#mode-full');
const modeUpdate = $$('#mode-update');
const download = $$('#download-firmware');
let activeMode = 'full';
let buildAvailable = false;

function setStatus(text, type) {
  releaseStatus.textContent = text;
  led.classList.remove('ok', 'fail');
  if (type) led.classList.add(type);
}
function setMode(mode) {
  activeMode = mode;
  modeFull.classList.toggle('active', mode === 'full');
  modeUpdate.classList.toggle('active', mode === 'update');
  modeFull.setAttribute('aria-pressed', String(mode === 'full'));
  modeUpdate.setAttribute('aria-pressed', String(mode === 'update'));
  installer.setAttribute('manifest', mode === 'full' ? '/firmware/manifest-factory.json' : '/firmware/manifest-update.json');
  if (buildAvailable) {
    const activate = installer.querySelector('[slot="activate"]');
    activate.textContent = mode === 'full' ? 'Connect & install firmware' : 'Connect & update application';
    download.href = mode === 'full' ? '/firmware/factory.bin' : '/firmware/firmware.bin';
    download.textContent = mode === 'full' ? 'Download factory binary ↗' : 'Download application binary ↗';
  }
}
modeFull.addEventListener('click', () => setMode('full'));
modeUpdate.addEventListener('click', () => setMode('update'));

async function loadFirmware() {
  try {
    const [metaResponse, manifestResponse, factoryResponse, updateResponse] = await Promise.all([
      fetch('/firmware/release.json', {cache:'no-store'}),
      fetch('/firmware/manifest-factory.json', {cache:'no-store'}),
      fetch('/firmware/factory.bin', {method:'HEAD', cache:'no-store'}),
      fetch('/firmware/firmware.bin', {method:'HEAD', cache:'no-store'})
    ]);
    if (![metaResponse, manifestResponse, factoryResponse, updateResponse].every(res => res.ok)) throw new Error('Firmware is not yet published');
    const meta = await metaResponse.json();
    const manifest = await manifestResponse.json();
    if (!meta.source_commit || !meta.version || meta.chip_family !== 'ESP32' || manifest.builds?.[0]?.chipFamily !== 'ESP32') throw new Error('Firmware manifest is invalid');
    if (typeof meta.factory_size_bytes !== 'number' || meta.factory_size_bytes < 50000) throw new Error('Firmware binary is unexpectedly small');
    $$('#build-info').hidden = false;
    $$('#build-version').textContent = meta.version;
    $$('#build-sha').textContent = String(meta.source_commit).slice(0,10);
    setStatus('Latest firmware ready', 'ok');
    await import('/vendor/esp-web-tools/install-button.js');
    buildAvailable = true;
    placeholder.hidden = true;
    installer.hidden = false;
    setMode(activeMode);
  } catch (err) {
    setStatus('Awaiting first successful GitHub build', 'fail');
    placeholder.textContent = 'The installer will appear after GitHub Actions successfully compiles and publishes firmware for this exact device. Check the build status on GitHub.';
    console.info('Firmware unavailable:', err instanceof Error ? err.message : err);
  }
}
loadFirmware();

for (const img of document.querySelectorAll('.photo-frame img')) {
  img.addEventListener('error', () => img.closest('.photo-frame')?.classList.add('photo-missing'));
}
const photo = document.querySelector('.photo-frame img');
if (photo?.complete && photo.naturalWidth === 0) photo.closest('.photo-frame')?.classList.add('photo-missing');

const video = $$('#play-video');
video.addEventListener('click', () => {
  const frame = document.createElement('iframe');
  frame.src = 'https://www.youtube-nocookie.com/embed/In7P1azFg-0?autoplay=1&rel=0';
  frame.title = 'I Put 8 Admin Tools on a Tiny ESP32 Touchscreen';
  frame.allow = 'accelerometer; autoplay; encrypted-media; gyroscope; picture-in-picture; web-share';
  frame.referrerPolicy = 'strict-origin-when-cross-origin';
  frame.allowFullscreen = true;
  $$('#video-frame').replaceChildren(frame);
});

if ('IntersectionObserver' in window) {
  const observer = new IntersectionObserver((entries) => {
    for (const entry of entries) if (entry.isIntersecting) {entry.target.classList.add('in-view'); observer.unobserve(entry.target);}
  }, {rootMargin: '0px 0px -35px 0px', threshold:.06});
  document.querySelectorAll('.reveal').forEach(el => observer.observe(el));
} else {
  document.querySelectorAll('.reveal').forEach(el => el.classList.add('in-view'));
}
