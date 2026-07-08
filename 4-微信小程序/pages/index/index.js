import Toast from '@vant/weapp/toast/toast';
import { Book } from '../../model/book';

const app = getApp();

Page({
  data: {
    deviceOnline: false,
    slots: [],           // 16 slots with book title overlay
    isAdmin: false
  },

  onLoad() {
    this.setData({ isAdmin: app.isAdmin() });
    this.loadAll();
  },

  onShow() {
    this.setData({ isAdmin: app.isAdmin() });
    this.loadAll();
  },

  async loadAll() {
    try {
      const [slotRes, shelfRes] = await Promise.all([
        Book.getStorageSlots(),
        Book.getShelfBooks()
      ]);
      if (slotRes.code === 200) {
        const slots = slotRes.data.map(s => {
          const book = (shelfRes.data || []).find(b => b.slot_id === s.slot_id);
          return {
            ...s,
            bookTitle: book ? book.title : '',
            bookAuthor: book ? book.author : '',
            bookId: book ? book.book_id : 0,
            instanceId: book ? book.instance_id : 0
          };
        });
        this.setData({ slots });
      }
    } catch (e) {
      console.error('加载书架失败', e);
    }
    this.checkDevice();
  },

  async checkDevice() {
    try {
      const res = await Book.getDeviceStatus();
      this.setData({ deviceOnline: !!(res && res.code === 200 && res.data) });
    } catch (e) {
      this.setData({ deviceOnline: false });
    }
  },

  onSlotTap(e) {
    const slot = e.currentTarget.dataset.slot;
    if (slot.instanceId) {
      wx.navigateTo({ url: '/pages/scan/scan?instanceId=' + slot.instanceId });
    }
  },

  goScan() {
    wx.navigateTo({ url: '/pages/scan/scan' });
  },

  goEntry() {
    wx.navigateTo({ url: '/pages/entry/entry' });
  },

  goLogs() {
    wx.navigateTo({ url: '/pages/logs/logs' });
  },

  goMine() {
    wx.switchTab({ url: '/pages/mine/mine' });
  },

  goSearch() {
    wx.navigateTo({ url: '/pages/inventory/inventory' });
  }
});
