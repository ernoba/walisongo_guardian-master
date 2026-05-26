// ============================================================================
// PERFORMANCE & LAZY LOADING UTILITIES
// ============================================================================

class PerformanceManager {
    constructor() {
        this.requestCache = new Map();
        this.imageCache = new Map();
        this.scrollObserver = null;
        this.imageObserver = null;
        this.initObservers();
    }

    // ── INITIALIZATION ─────────────────────────────────────
    initObservers() {
        // Observer untuk lazy load gambar
        this.imageObserver = new IntersectionObserver(
            (entries) => {
                entries.forEach((entry) => {
                    if (entry.isIntersecting) {
                        this.loadImage(entry.target);
                        this.imageObserver.unobserve(entry.target);
                    }
                });
            },
            { rootMargin: '100px', threshold: 0.01 }
        );

        // Observer untuk infinite scroll
        this.scrollObserver = new IntersectionObserver(
            (entries) => {
                entries.forEach((entry) => {
                    if (entry.isIntersecting && entry.target.dataset.callback) {
                        const callback = window[entry.target.dataset.callback];
                        if (typeof callback === 'function') {
                            callback();
                        }
                    }
                });
            },
            { threshold: 0.1 }
        );
    }

    // ── CACHING ────────────────────────────────────────────
    async getCachedRequest(url, options = {}) {
        const cacheKey = url + JSON.stringify(options);

        // Check memory cache first
        if (this.requestCache.has(cacheKey)) {
            const cached = this.requestCache.get(cacheKey);
            if (Date.now() - cached.time < (options.cacheDuration || 5 * 60 * 1000)) {
                return cached.data;
            }
        }

        try {
            const response = await fetch(url, options);
            const data = await response.json();

            // Store in memory cache
            this.requestCache.set(cacheKey, {
                data,
                time: Date.now(),
            });

            // Limit cache size (keep last 50 requests)
            if (this.requestCache.size > 50) {
                const firstKey = this.requestCache.keys().next().value;
                this.requestCache.delete(firstKey);
            }

            return data;
        } catch (error) {
            console.error('Cache request failed:', error);
            throw error;
        }
    }

    // ── IMAGE LOADING ──────────────────────────────────────
    async loadImage(imgElement) {
        const fileId = imgElement.dataset.fileId;
        if (!fileId) return;

        // Show placeholder while loading
        imgElement.classList.add('loading');

        try {
            // Check memory cache
            if (this.imageCache.has(fileId)) {
                imgElement.src = this.imageCache.get(fileId);
                imgElement.classList.remove('loading');
                return;
            }

            // Fetch binary image data. Base64 JSON previews inflate memory by ~33%
            // and become painful when hundreds of screenshots are visible.
            const response = await fetch(`/dashboard/api/file-preview/${encodeURIComponent(fileId)}?raw=true`, {
                headers: { 'X-API-Key': 'ADMIN-2025' },
                cache: 'force-cache',
            });
            if (!response.ok) throw new Error(`${response.status} ${response.statusText}`);
            const objectUrl = URL.createObjectURL(await response.blob());

            // Cache in memory
            this.imageCache.set(fileId, objectUrl);
            imgElement.src = objectUrl;
            imgElement.classList.remove('loading');
        } catch (error) {
            console.error('Image load failed:', error);
            imgElement.classList.remove('loading');
            imgElement.style.background = 'var(--surface4)';
        }
    }

    observeImage(imgElement) {
        if (this.imageObserver) {
            this.imageObserver.observe(imgElement);
        }
    }

    observeScroll(element, callback) {
        if (this.scrollObserver) {
            element.dataset.callback = callback;
            this.scrollObserver.observe(element);
        }
    }

    clearCache() {
        this.requestCache.clear();
        for (const url of this.imageCache.values()) {
            if (typeof url === 'string' && url.startsWith('blob:')) {
                URL.revokeObjectURL(url);
            }
        }
        this.imageCache.clear();
    }
}

// ============================================================================
// VIRTUAL SCROLLING (untuk table besar)
// ============================================================================

class VirtualScroller {
    constructor(container, itemHeight, bufferSize = 5) {
        this.container = container;
        this.itemHeight = itemHeight;
        this.bufferSize = bufferSize;
        this.items = [];
        this.scrollTop = 0;
        this.visibleStart = 0;
        this.visibleEnd = 0;
        this.resizeObserver = null;
        this.containerHeight = 0;
        this.init();
    }

    init() {
        this.container.style.overflow = 'auto';
        this.container.style.height = '100%';

        this.container.addEventListener('scroll', () => {
            this.updateVisibleRange();
        });

        this.resizeObserver = new ResizeObserver(() => {
            this.containerHeight = this.container.clientHeight;
            this.updateVisibleRange();
        });
        this.resizeObserver.observe(this.container);

        this.containerHeight = this.container.clientHeight;
    }

    setItems(items) {
        this.items = items;
        this.render();
    }

    updateVisibleRange() {
        this.scrollTop = this.container.scrollTop;
        const visibleCount = Math.ceil(this.containerHeight / this.itemHeight) + this.bufferSize * 2;
        this.visibleStart = Math.max(0, Math.floor(this.scrollTop / this.itemHeight) - this.bufferSize);
        this.visibleEnd = Math.min(this.items.length, this.visibleStart + visibleCount);
        this.render();
    }

    render() {
        const visibleItems = this.items.slice(this.visibleStart, this.visibleEnd);
        const offsetY = this.visibleStart * this.itemHeight;

        const html = visibleItems.map(item => this.renderItem(item)).join('');

        this.container.innerHTML = `
      <div style="height:${this.items.length * this.itemHeight}px;position:relative;">
        <div style="transform:translateY(${offsetY}px);will-change:transform;">
          ${html}
        </div>
      </div>
    `;
    }

    renderItem(item) {
        // Override ini di subclass atau pass function
        return `<div style="height:${this.itemHeight}px;display:flex;align-items:center;padding:0 12px;border-bottom:1px solid var(--border);">${item.name}</div>`;
    }

    destroy() {
        if (this.resizeObserver) {
            this.resizeObserver.disconnect();
        }
    }
}

// ============================================================================
// PAGINATION HELPER
// ============================================================================

class PaginationHelper {
    constructor(totalItems, pageSize = 20) {
        this.totalItems = totalItems;
        this.pageSize = pageSize;
        this.currentPage = 1;
        this.totalPages = Math.ceil(totalItems / pageSize);
    }

    setTotal(total) {
        this.totalItems = total;
        this.totalPages = Math.ceil(total / this.pageSize);
        if (this.currentPage > this.totalPages) {
            this.currentPage = Math.max(1, this.totalPages);
        }
    }

    getPageItems(items) {
        const start = (this.currentPage - 1) * this.pageSize;
        return items.slice(start, start + this.pageSize);
    }

    nextPage() {
        if (this.currentPage < this.totalPages) {
            this.currentPage++;
            return true;
        }
        return false;
    }

    prevPage() {
        if (this.currentPage > 1) {
            this.currentPage--;
            return true;
        }
        return false;
    }

    goToPage(page) {
        if (page >= 1 && page <= this.totalPages) {
            this.currentPage = page;
            return true;
        }
        return false;
    }

    getInfo() {
        const start = (this.currentPage - 1) * this.pageSize + 1;
        const end = Math.min(this.currentPage * this.pageSize, this.totalItems);
        return { start, end, total: this.totalItems, page: this.currentPage, pages: this.totalPages };
    }
}

// ============================================================================
// INFINITE SCROLL HELPER
// ============================================================================

class InfiniteScroller {
    constructor(containerSelector, options = {}) {
        this.container = document.querySelector(containerSelector);
        this.pageSize = options.pageSize || 20;
        this.currentPage = 0;
        this.isLoading = false;
        this.hasMore = true;
        this.onLoadMore = options.onLoadMore || (() => { });
        this.loadingTemplate = options.loadingTemplate || this.defaultLoadingTemplate();

        this.init();
    }

    init() {
        const sentinel = document.createElement('div');
        sentinel.className = 'infinite-scroll-sentinel';
        sentinel.style.height = '100px';
        this.container.appendChild(sentinel);

        const observer = new IntersectionObserver(
            (entries) => {
                if (entries[0].isIntersecting && !this.isLoading && this.hasMore) {
                    this.loadMore();
                }
            },
            { threshold: 0.1 }
        );
        observer.observe(sentinel);
        this.sentinel = sentinel;
        this.observer = observer;
    }

    async loadMore() {
        if (this.isLoading || !this.hasMore) return;

        this.isLoading = true;
        this.showLoading();

        try {
            const result = await this.onLoadMore(this.currentPage);
            this.currentPage++;
            this.hasMore = result.hasMore ?? true;

            if (!this.hasMore) {
                this.sentinel.style.display = 'none';
            }
        } catch (error) {
            console.error('Infinite scroll load error:', error);
        } finally {
            this.isLoading = false;
            this.hideLoading();
        }
    }

    showLoading() {
        if (!document.querySelector('.infinite-scroll-loading')) {
            const loadingEl = document.createElement('div');
            loadingEl.className = 'infinite-scroll-loading';
            loadingEl.innerHTML = this.loadingTemplate;
            this.sentinel.before(loadingEl);
        }
    }

    hideLoading() {
        const loadingEl = document.querySelector('.infinite-scroll-loading');
        if (loadingEl) loadingEl.remove();
    }

    reset() {
        this.currentPage = 0;
        this.hasMore = true;
        this.isLoading = false;
    }

    destroy() {
        if (this.observer) this.observer.disconnect();
    }

    defaultLoadingTemplate() {
        return `
      <div style="padding:20px;text-align:center;color:var(--text3);">
        <div class="spinner" style="margin:0 auto 10px;"></div>
        <div style="font-size:13px;">Memuat lebih banyak…</div>
      </div>
    `;
    }
}

// ============================================================================
// BLUR-UP EFFECT UNTUK IMAGES
// ============================================================================

function initBlurUpEffect(imgElement, placeholderDataUrl) {
    if (placeholderDataUrl) {
        imgElement.src = placeholderDataUrl;
        imgElement.style.filter = 'blur(10px)';
        imgElement.style.transition = 'filter 0.3s ease-out';
    }

    imgElement.addEventListener('load', () => {
        imgElement.style.filter = 'blur(0)';
    });
}

// ============================================================================
// LAZY LOAD TEXT CONTENT
// ============================================================================

const textContentObserver = new IntersectionObserver(
    (entries) => {
        entries.forEach((entry) => {
            if (entry.isIntersecting && entry.target.dataset.content) {
                entry.target.textContent = entry.target.dataset.content;
                entry.target.removeAttribute('data-content');
                textContentObserver.unobserve(entry.target);
            }
        });
    },
    { rootMargin: '50px' }
);

// ============================================================================
// BATCHING API REQUESTS
// ============================================================================

class BatchedAPIClient {
    constructor(batchSize = 10, batchDelay = 50) {
        this.batchSize = batchSize;
        this.batchDelay = batchDelay;
        this.queue = [];
        this.timer = null;
    }

    add(url, options = {}) {
        return new Promise((resolve, reject) => {
            this.queue.push({ url, options, resolve, reject });

            if (this.queue.length >= this.batchSize) {
                this.flush();
            } else if (!this.timer) {
                this.timer = setTimeout(() => this.flush(), this.batchDelay);
            }
        });
    }

    async flush() {
        if (this.timer) clearTimeout(this.timer);
        this.timer = null;

        const batch = this.queue.splice(0, this.batchSize);
        if (batch.length === 0) return;

        try {
            const promises = batch.map((req) =>
                fetch(req.url, req.options)
                    .then((r) => r.json())
                    .then((data) => {
                        req.resolve(data);
                        return data;
                    })
                    .catch((error) => {
                        req.reject(error);
                        throw error;
                    })
            );

            await Promise.allSettled(promises);
        } catch (error) {
            console.error('Batch request failed:', error);
        }

        if (this.queue.length > 0) {
            this.timer = setTimeout(() => this.flush(), this.batchDelay);
        }
    }

    async waitAll() {
        while (this.queue.length > 0 || this.timer) {
            await new Promise((resolve) => setTimeout(resolve, 10));
        }
    }
}

// ============================================================================
// EXPORT UTILITIES
// ============================================================================

if (typeof window !== 'undefined') {
    window.PerformanceManager = PerformanceManager;
    window.VirtualScroller = VirtualScroller;
    window.PaginationHelper = PaginationHelper;
    window.InfiniteScroller = InfiniteScroller;
    window.BatchedAPIClient = BatchedAPIClient;
    window.initBlurUpEffect = initBlurUpEffect;
    window.textContentObserver = textContentObserver;
}
