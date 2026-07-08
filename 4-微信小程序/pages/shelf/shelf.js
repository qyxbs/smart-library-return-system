/**
 * 书架 Tab — 16 格可视化书架
 * 用户浏览、搜索、查看书本详情
 */
const { Book } = require('../../model/book');

Page({
  data: {
    deviceOnline: false,
    slots: [],        // merged slot + book data
    loading: true,
  },

  onLoad() {
    this.loadShelf();
  },

  onShow() {
    this.loadShelf();
  },

  async loadShelf() {
    this.setData({ loading: true });
    try {
      const [slotRes, shelfRes] = await Promise.all([
        Book.getSlots(),
        Book.getShelfBooks()
      ]);

      if (slotRes.code === 200) {
        const shelfMap = {};
        if (shelfRes.code === 200 && shelfRes.data) {
          shelfRes.data.forEach(b => { shelfMap[b.slot_id] = b; });
        }

        const slots = slotRes.data.map(s => {
          const book = shelfMap[s.slot_id];
          return {
            ...s,
            bookTitle: book ? book.title : '',
            bookId: book ? book.book_id : 0,
            instanceId: book ? book.instance_id : 0,
            category: book ? book.category : '',
            isOccupied: !!book,
          };
        });

        this.setData({ slots });
      }
    } catch (e) {
      console.error('加载书架失败', e);
    }
    this.setData({ loading: false });
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
    const { slotId } = e.detail;
    // Find slot data from our list
    const slot = this.data.slots.find(s => s.slot_id == slotId);
    if (slot && slot.instanceId) {
      wx.navigateTo({ url: '/pages/book-detail/book-detail?instanceId=' + slot.instanceId });
    }
  },

  onGoSearch() {
    wx.navigateTo({ url: '/pages/search/search' });
  },

  onRefresh() {
    this.loadShelf();
  }
});
