import Toast from '@vant/weapp/toast/toast';
import { Book } from '../../model/book';

const app = getApp();

Page({
  data: {
    keyword: '',
    results: [],
    shelfBooks: [],
    isAdmin: false,
    searching: false
  },

  onLoad() {
    this.setData({ isAdmin: app.isAdmin() });
    this.loadShelf();
  },

  onShow() {
    this.setData({ isAdmin: app.isAdmin() });
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

  onSearchInput(e) {
    this.setData({ keyword: e.detail.value });
  },

  async onSearch() {
    const kw = this.data.keyword.trim();
    if (!kw) {
      this.loadShelf();
      return;
    }
    this.setData({ searching: true });
    try {
      const res = await Book.searchBook(kw);
      if (res.code === 200) {
        this.setData({ results: res.data || [] });
      }
    } catch (e) {
      Toast.fail('搜索失败');
    }
    this.setData({ searching: false });
  },

  onBookTap(e) {
    const book = e.currentTarget.dataset.book;
    // Find instance for this book
    wx.navigateTo({ url: '/pages/scan/scan?instanceId=' + book.instance_id });
  },

  goScan() {
    wx.navigateTo({ url: '/pages/scan/scan' });
  },

  statusText(status) {
    const map = { 0: '待贴标', 1: '已借出', 2: '待上架', 3: '在架' };
    return map[status] || '未知';
  }
});
