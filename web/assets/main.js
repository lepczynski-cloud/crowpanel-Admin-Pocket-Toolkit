'use strict';

const $ = (selector) => document.querySelector(selector);
const led = $('#release-led');
const releaseStatus = $('#release-status');
const installer = $('#web-install');
const placeholder = $('#installer-loading');
const modeFull = $('#mode-full');
const modeUpdate = $('#mode-update');
const download = $('#download-firmware');
const retry = $('#retry-firmware');
const diagnostics = $('#firmware-diagnostics');
const diagnosticsLink = $('#firmware-diagnostics-link');
const base = new URL('/', location.href);
let activeMode = 'full';
let buildAvailable = false;
let checking = false;

function localUrl(path) {
  return new URL(path.replace(/^\//, ''), base).href;
}

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
  installer.setAttribute('manifest', localUrl(mode === 'full' ? 'firmware/manifest-factory.json' : 'firmware/manifest-update.json'));
  if (buildAvailable) {
    const activate = installer.querySelector('[slot="activate"]');
    activate.firstChild.textContent = mode === 'full' ? 'Connect & install firmware ' : 'Connect & update application ';
    download.href = localUrl(mode === 'full' ? 'firmware/factory.bin' : 'firmware/firmware.bin');
    download.firstChild.textContent = mode === 'full' ? 'Download factory binary ' : 'Download application binary ';
  }
}

modeFull.addEventListener('click', () => setMode('full'));
modeUpdate.addEventListener('click', () => setMode('update'));
retry.addEventListener('click', () => loadFirmware());

async function getJson(path) {
  const response = await fetch(localUrl(path), { cache: 'no-store', headers: { Accept: 'application/json' } });
  if (!response.ok) {
    throw new Error(`${path} returned HTTP ${response.status}. Cloudflare Pages may still be using the old settings. Check Cloudflare Pages: production branch main, build command bash build-cloudflare.sh, output dist.`);
  }
  const contentType = response.headers.get('content-type') || '';
  if (contentType.includes('text/html')) {
    throw new Error(`${path} returned an HTML page instead of JSON. Cloudflare Pages must publish the dist directory built from main.`);
  }
  try {
    return await response.json();
  } catch (_error) {
    throw new Error(`${path} is not valid JSON. Check the Cloudflare Pages build logs and dist/firmware output.`);
  }
}

function validateManifest(manifest, expectedPath, sourceVersion) {
  const builds = manifest?.builds;
  if (!Array.isArray(builds) || builds.length !== 1 || builds[0].chipFamily !== 'ESP32') {
    throw new Error(`firmware/${expectedPath} has no ESP32 build definition.`);
  }
  if (manifest.version !== sourceVersion) {
    throw new Error(`firmware/${expectedPath} does not match the current release version.`);
  }
  if (!Array.isArray(builds[0].parts) || builds[0].parts.length !== 1) {
    throw new Error(`firmware/${expectedPath} contains an unexpected flash layout.`);
  }
  return builds[0].parts[0];
}

async function loadFirmware() {
  if (checking) return;
  checking = true;
  retry.disabled = true;
  diagnostics.hidden = true;
  diagnosticsLink.hidden = true;
  placeholder.hidden = false;
  installer.hidden = true;
  buildAvailable = false;
  setStatus('Checking published firmware', '');
  placeholder.textContent = 'Checking firmware and installation manifests on this domain...';
  try {
    // NOTE: Do not send HEAD requests for binaries as a prerequisite for the installer.
    // Some CDN configurations reject HEAD while the same files can be downloaded via GET.
    // The Cloudflare build has already checked binary sizes, offsets, hashes and manifests.
    const meta = await getJson('firmware/release.json');
    if (!meta?.source_commit || !meta.version || meta.chip_family !== 'ESP32' ||
        !Number.isSafeInteger(meta.factory_size_bytes) || meta.factory_size_bytes < 50000 ||
        !Number.isSafeInteger(meta.firmware_size_bytes) || meta.firmware_size_bytes < 50000) {
      throw new Error('firmware/release.json is incomplete or not for this ESP32 device.');
    }
    const factory = await getJson('firmware/manifest-factory.json');
    const update = await getJson('firmware/manifest-update.json');
    const factoryPart = validateManifest(factory, 'manifest-factory.json', meta.version);
    const updatePart = validateManifest(update, 'manifest-update.json', meta.version);
    if (factoryPart.path !== 'factory.bin' || factoryPart.offset !== 0 ||
        updatePart.path !== 'firmware.bin' || updatePart.offset !== 65536) {
      throw new Error('Firmware offsets/filenames do not match the supported CrowPanel flash layout.');
    }
    // Use the fully vendored Web Tools release. Nothing is downloaded from a third-party CDN.
    await import(localUrl('vendor/esp-web-tools/install-button.js'));
    $('#build-info').hidden = false;
    $('#build-version').textContent = meta.version;
    $('#build-sha').textContent = String(meta.source_commit).slice(0, 10);
    setStatus('Latest firmware ready', 'ok');
    buildAvailable = true;
    setMode(activeMode);
    placeholder.hidden = true;
    installer.hidden = false;
  } catch (error) {
    setStatus('Firmware is not available on this website', 'fail');
    const reason = error instanceof Error ? error.message : String(error);
    placeholder.textContent = `Installer unavailable: ${reason}`;
    diagnostics.hidden = false;
    diagnostics.textContent = 'Check your Cloudflare Pages deployment: production branch must be main, build command bash build-cloudflare.sh, build output dist. Open the release manifest below for the HTTP error, then check the Pages deployment log.';
    diagnosticsLink.href = localUrl('firmware/release.json');
    diagnosticsLink.hidden = false;
    console.warn('Admin Pocket Toolkit: firmware installer unavailable:', reason);
  } finally {
    checking = false;
    retry.disabled = false;
  }
}

loadFirmware();

// Use the locally hosted real YouTube cover. Fall back to YouTube thumbnails only
// if a previously deployed site does not contain the JPEG yet.
const videoCoverFallbacks = [
  'https://i.ytimg.com/vi/In7P1azFg-0/hqdefault.jpg',
  'https://img.youtube.com/vi/In7P1azFg-0/hqdefault.jpg'
];
for (const img of document.querySelectorAll('img[data-device-photo]')) {
  let fallbackIndex = 0;
  const useNextCover = () => {
    if (fallbackIndex < videoCoverFallbacks.length) {
      img.src = videoCoverFallbacks[fallbackIndex++];
      return;
    }
    img.closest('.photo-frame')?.classList.add('photo-missing');
    if (img.closest('.video-poster')) img.style.visibility = 'hidden';
  };
  img.addEventListener('error', useNextCover);
  if (img.complete && img.naturalWidth === 0) useNextCover();
}

const video = $('#play-video');
video.addEventListener('click', () => {
  const frame = document.createElement('iframe');
  frame.src = 'https://www.youtube-nocookie.com/embed/In7P1azFg-0?autoplay=1&rel=0';
  frame.title = 'I Put 8 Admin Tools on a Tiny ESP32 Touchscreen';
  frame.allow = 'accelerometer; autoplay; encrypted-media; gyroscope; picture-in-picture; web-share';
  frame.referrerPolicy = 'strict-origin-when-cross-origin';
  frame.allowFullscreen = true;
  $('#video-frame').replaceChildren(frame);
});
if ('IntersectionObserver' in window) {
  const observer = new IntersectionObserver((entries) => {
    for (const entry of entries) if (entry.isIntersecting) {
      entry.target.classList.add('in-view');
      observer.unobserve(entry.target);
    }
  }, {rootMargin: '0px 0px -35px 0px', threshold: .06});
  document.querySelectorAll('.reveal').forEach(el => observer.observe(el));
} else {
  document.querySelectorAll('.reveal').forEach(el => el.classList.add('in-view'));
}
