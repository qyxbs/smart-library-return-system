/**
 * 搜索页 — 搜索书本 + 在架列表
 */
const { Book } = require('../../model/book');

Page({
  data: {
    keyword: '',
    results: [],
    shelfBooks: [],
    searching: false,
    searched: false,
  },

  onLoad() {
    this.loadShelf();
  },

  async loadShelf() {
    try {
      const res = await Book.getShelfBooks();
      if (res.code === 200) {
        this.setData({ shelfBooks: res.data || [] });
      }
    } catch (e) {
      console.error('加载书架失败', e);
    }
  },

  onInput(e) {
    const val = typeof e.detail === 'string' ? e.detail : (e.detail.value || '');
    this.setData({ keyword: val });
    if (!val.trim()) {
      this.setData({ results: [], searched: false });
    }
  },

  async onSearch() {
    const kw = this.data.keyword.trim();
    if (!kw) {
      this.setData({ results: [], searched: false });
      return;
    }

    this.setData({ searching: true, searched: true });
    try {
      const res = await Book.searchBook(kw);
      if (res.code === 200) {
        this.setData({ results: res.data || [] });
      }
    } catch (e) {
      wx.showToast({ title: '搜索失败', icon: 'none' });
    }
    this.setData({ searching: false });
  },

  onClear() {
    this.setData({ keyword: '', results: [], searched: false });
  },

  onBookTap(e) {
    const idx = e.currentTarget.dataset.index;
    const book = this.data.results[idx];
    if (!book) return;

    const shelfBook = this.data.shelfBooks.find(b => b.book_id == book.id);
    if (shelfBook) {
      wx.navigateTo({ url: '/pages/book-detail/book-detail?instanceId=' + shelfBook.instance_id });
    } else {
      wx.navigateTo({ url: '/pages/book-detail/book-detail?bookId=' + book.id +
        '&title=' + encodeURIComponent(book.title || '') +
        '&category=' + encodeURIComponent(book.category || '')
      });
    }
  },

  onShelfBookTap(e) {
    const idx = e.currentTarget.dataset.index;
    const book = this.data.shelfBooks[idx];
    if (!book || !book.instance_id) {
      wx.showToast({ title: '数据错误', icon: 'none' });
      return;
    }
    wx.navigateTo({ url: '/pages/book-detail/book-detail?instanceId=' + book.instance_id });
  }
});
