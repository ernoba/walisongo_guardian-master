// Service Worker untuk caching dan performa
const CACHE_VERSION = 'v2-guardian-paged';
const URLS_TO_CACHE = [
    '/',
    '/index.html',
    'https://fonts.googleapis.com',
    'https://cdnjs.cloudflare.com/ajax/libs/Chart.js/4.4.1/chart.umd.min.js',
];

const API_CACHE = 'api-cache-v1';
const IMAGE_CACHE = 'image-cache-v1';
const CACHE_DURATION = 5 * 60 * 1000; // 5 menit

let cacheTimestamps = {};

self.addEventListener('install', (event) => {
    console.log('[SW] Installing service worker...');
    event.waitUntil(
        caches.open(CACHE_VERSION).then((cache) => {
            console.log('[SW] Cache opened');
            return cache.addAll(URLS_TO_CACHE).catch(() => {
                console.log('[SW] Some resources failed to cache (offline OK)');
            });
        })
    );
    self.skipWaiting();
});

self.addEventListener('activate', (event) => {
    console.log('[SW] Activating service worker...');
    event.waitUntil(
        caches.keys().then((cacheNames) => {
            return Promise.all(
                cacheNames.map((cacheName) => {
                    if (cacheName !== CACHE_VERSION) {
                        console.log('[SW] Deleting old cache:', cacheName);
                        return caches.delete(cacheName);
                    }
                })
            );
        })
    );
    self.clients.claim();
});

// Strategi: jangan cache payload API besar. Dashboard sekarang memakai
// pagination dan blob streaming; menyimpan response API di Cache Storage justru
// bisa membuat browser memegang ratusan MB data lama.
self.addEventListener('fetch', (event) => {
    const { request } = event;
    const url = new URL(request.url);

    // Skip non-GET requests
    if (request.method !== 'GET') return;

    // API calls: Network only supaya data besar/preview tidak tersimpan diam-diam.
    if (url.pathname.includes('/dashboard/api/')) {
        event.respondWith(fetch(request).catch(() => new Response('Offline', { status: 503 })));
        return;
    }

    // Images: Cache-First
    if (/\.(jpg|jpeg|png|gif|webp|svg)(\?|$)/i.test(url.pathname)) {
        event.respondWith(handleImageRequest(request));
        return;
    }

    // HTML, CSS, JS: Network-First
    event.respondWith(handleAssetRequest(request));
});

async function handleAPIRequest(request) {
    const cacheKey = request.url;
    const now = Date.now();
    const cached = await caches.match(cacheKey, { cacheName: API_CACHE });

    // Return cached if fresh
    if (cached) {
        const timestamp = cacheTimestamps[cacheKey] || 0;
        if (now - timestamp < CACHE_DURATION) {
            return cached;
        }
    }

    try {
        const response = await fetch(request);
        if (response.ok) {
            const cacheable = response.clone();
            caches.open(API_CACHE).then((cache) => {
                cache.put(cacheKey, cacheable);
                cacheTimestamps[cacheKey] = now;
            });
        }
        return response;
    } catch {
        // Return stale cache on network error
        if (cached) return cached;
        return new Response('Offline', { status: 503 });
    }
}

async function handleImageRequest(request) {
    const cached = await caches.match(request.url, { cacheName: IMAGE_CACHE });
    if (cached) return cached;

    try {
        const response = await fetch(request);
        if (response.ok) {
            const cacheable = response.clone();
            caches.open(IMAGE_CACHE).then((cache) => {
                cache.put(request.url, cacheable);
            });
        }
        return response;
    } catch {
        return new Response('Image not found', { status: 404 });
    }
}

async function handleAssetRequest(request) {
    try {
        const response = await fetch(request);
        if (response.ok) {
            const cacheable = response.clone();
            caches.open(CACHE_VERSION).then((cache) => {
                cache.put(request.url, cacheable);
            });
        }
        return response;
    } catch {
        const cached = await caches.match(request.url, { cacheName: CACHE_VERSION });
        return cached || new Response('Offline', { status: 503 });
    }
}

// Clear cache periodically
setInterval(() => {
    const now = Date.now();
    Object.keys(cacheTimestamps).forEach((key) => {
        if (now - cacheTimestamps[key] > CACHE_DURATION * 2) {
            delete cacheTimestamps[key];
        }
    });
}, 60000);
