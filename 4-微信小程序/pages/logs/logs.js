import { Book } from '../../model/book';

Page({
  data: {
    logs: [],
    page: 1,
    loading: false,
    hasMore: true
  },

  onLoad() {
    this.loadLogs();
  },

  async loadLogs() {
    if (this.data.loading || !this.data.hasMore) return;
    this.setData({ loading: true });
    try {
      const res = await Book.getLogs({ page: this.data.page, size: 20 });
      if (res.code === 200) {
        const records = res.data.records || [];
        this.setData({
          logs: [...this.data.logs, ...records],
          page: this.data.page + 1,
          hasMore: records.length >= 20
        });
      }
    } catch (e) {
      console.error('加载日志失败', e);
    }
    this.setData({ loading: false });
  },

  actionText(type) {
    const map = {
      'PRINT': '打印标签',
      'SCAN': '入库上架',
      'RETRIEVE': '取书',
      'BORROW': '借书',
      'RETURN': '还书',
      'RE_SHELVE': '重新上架',
      'VERIFY': '回收确认'
    };
    return map[type] || type;
  },

  onReachBottom() {
    this.loadLogs();
  }
});
