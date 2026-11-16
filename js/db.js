/* IndexedDB storage layer.
 * Stores:
 *   meta          key/value (settings, etc.)
 *   categories    {id, name, type, icon, budget, createdAt}
 *   transactions  {id, type, categoryId, amount(cents), date(YYYY-MM-DD), description, vendor, paymentMethod, notes, createdAt, updatedAt}
 *   receipts      {id, transactionId, name, mime, size, blob, createdAt}
 */
const DB = (() => {
  const NAME = 'ledgerly';
  const VERSION = 1;
  const STORES = ['meta', 'categories', 'transactions', 'receipts'];
  let db = null;

  function open() {
    return new Promise((resolve, reject) => {
      if (!('indexedDB' in window)) {
        reject(new Error('This browser does not support IndexedDB storage.'));
        return;
      }
      const req = indexedDB.open(NAME, VERSION);
      req.onupgradeneeded = () => {
        const d = req.result;
        if (!d.objectStoreNames.contains('meta')) d.createObjectStore('meta');
        if (!d.objectStoreNames.contains('categories')) d.createObjectStore('categories', { keyPath: 'id' });
        if (!d.objectStoreNames.contains('transactions')) {
          const s = d.createObjectStore('transactions', { keyPath: 'id' });
          s.createIndex('date', 'date');
          s.createIndex('categoryId', 'categoryId');
        }
        if (!d.objectStoreNames.contains('receipts')) {
          const s = d.createObjectStore('receipts', { keyPath: 'id' });
          s.createIndex('transactionId', 'transactionId');
        }
      };
      req.onsuccess = () => { db = req.result; resolve(db); };
      req.onerror = () => reject(req.error);
      req.onblocked = () => reject(new Error('Database is open in another tab with an older version. Close other tabs and reload.'));
    });
  }

  /* Run an operation inside a transaction; resolves when the transaction completes. */
  function run(stores, mode, fn) {
    return new Promise((resolve, reject) => {
      const t = db.transaction(stores, mode);
      let result;
      const ret = fn(t, (v) => { result = v; });
      if (ret && 'onsuccess' in ret) ret.onsuccess = () => { result = ret.result; };
      t.oncomplete = () => resolve(result);
      t.onerror = () => reject(t.error);
      t.onabort = () => reject(t.error || new Error('Transaction aborted'));
    });
  }

  const getAll = (store) => run(store, 'readonly', (t) => t.objectStore(store).getAll());
  const get = (store, key) => run(store, 'readonly', (t) => t.objectStore(store).get(key));
  const put = (store, value) => run(store, 'readwrite', (t) => { t.objectStore(store).put(value); });
  const del = (store, key) => run(store, 'readwrite', (t) => { t.objectStore(store).delete(key); });
  const putMany = (store, values) => run(store, 'readwrite', (t) => {
    const s = t.objectStore(store);
    values.forEach((v) => s.put(v));
  });
  const delMany = (store, keys) => run(store, 'readwrite', (t) => {
    const s = t.objectStore(store);
    keys.forEach((k) => s.delete(k));
  });

  const getMeta = (key) => run('meta', 'readonly', (t) => t.objectStore('meta').get(key));
  const setMeta = (key, value) => run('meta', 'readwrite', (t) => { t.objectStore('meta').put(value, key); });

  const receiptsFor = (transactionId) =>
    run('receipts', 'readonly', (t) => t.objectStore('receipts').index('transactionId').getAll(transactionId));

  /* Receipt metadata only (no blobs) — used for counts in lists. */
  const receiptIndex = () => run('receipts', 'readonly', (t, set) => {
    const out = [];
    const req = t.objectStore('receipts').openCursor();
    req.onsuccess = () => {
      const c = req.result;
      if (c) {
        const { blob, ...meta } = c.value;
        out.push(meta);
        c.continue();
      } else set(out);
    };
  });

  /* Delete transactions and all of their receipts atomically. */
  function deleteTransactions(ids) {
    const idSet = new Set(ids);
    return run(['transactions', 'receipts'], 'readwrite', (t) => {
      const ts = t.objectStore('transactions');
      ids.forEach((id) => ts.delete(id));
      const req = t.objectStore('receipts').openCursor();
      req.onsuccess = () => {
        const c = req.result;
        if (!c) return;
        if (idSet.has(c.value.transactionId)) c.delete();
        c.continue();
      };
    });
  }

  /* Replace everything (restore from backup). */
  function replaceAll({ settings, categories, transactions, receipts }) {
    return run(STORES, 'readwrite', (t) => {
      STORES.forEach((s) => t.objectStore(s).clear());
      if (settings) t.objectStore('meta').put(settings, 'settings');
      categories.forEach((c) => t.objectStore('categories').put(c));
      transactions.forEach((x) => t.objectStore('transactions').put(x));
      receipts.forEach((r) => t.objectStore('receipts').put(r));
    });
  }

  function clearAll() {
    return run(STORES, 'readwrite', (t) => STORES.forEach((s) => t.objectStore(s).clear()));
  }

  return {
    open, getAll, get, put, del, putMany, delMany, getMeta, setMeta,
    receiptsFor, receiptIndex, deleteTransactions, replaceAll, clearAll,
  };
})();
