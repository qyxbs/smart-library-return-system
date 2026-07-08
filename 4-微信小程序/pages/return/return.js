/**
 * 还书页 — 选择要归还的书 → 生成还书任务QR
 */
const { Book } = require('../../model/book');
const app = getApp();

Page({
  data: {
    user: null,
    borrowed: [],
    selectedIds: [],    // selected instance_ids
    loading: false,
    submitting: false,
  },

  onLoad() {
    const user = app.getUserInfo();
    this.setData({ user });
    if (user) {
      this.loadBorrowed(user.user_id);
    }
  },

  onShow() {
    const user = app.getUserInfo();
    this.setData({ user });
    if (user) {
      this.loadBorrowed(user.user_id);
    }
  },

  async loadBorrowed(userId) {
    this.setData({ loading: true });
    try {
      const res = await Book.getMyBorrowed(userId);
      if (res.code === 200) {
        const borrowed = (res.data || []).map(b => ({ ...b, checked: true }));
        this.setData({
          borrowed,
          selectedIds: borrowed.map(b => b.instance_id),
        });
      }
    } catch (e) {
      console.error('加载借阅失败', e);
    }
    this.setData({ loading: false });
  },

  onToggleSelect(e) {
    const idx = e.currentTarget.dataset.index;
    const book = this.data.borrowed[idx];
    if (!book) return;
    const id = book.instance_id;
    let selected = this.data.selectedIds;
    if (selected.includes(id)) {
      selected = selected.filter(s => s !== id);
    } else {
      selected = [...selected, id];
    }
    this.setData({ selectedIds: selected });
  },

  onSelectAll() {
    if (this.data.selectedIds.length === this.data.borrowed.length) {
      this.setData({ selectedIds: [] });
    } else {
      this.setData({ selectedIds: this.data.borrowed.map(b => b.instance_id) });
    }
  },

  /** 创建还书任务 → 跳转 QR 展示页 */
  async onSubmit() {
    if (!this.data.selectedIds.length) {
      wx.showToast({ title: '请选择要还的书', icon: 'none' });
      return;
    }

    const user = this.data.user;
    if (!user) {
      wx.showToast({ title: '请先登录', icon: 'none' });
      return;
    }

    this.setData({ submitting: true });
    try {
      const res = await Book.createTask('return', user.user_id, {
        instanceIds: this.data.selectedIds
      });

      if (res.code === 200) {
        const task = res.data;
        wx.redirectTo({
          url: '/pages/qr-show/qr-show?taskId=' + task.taskId +
               '&action=return&bookCount=' + this.data.selectedIds.length
        });
      } else {
        wx.showToast({ title: res.message || '创建任务失败', icon: 'none' });
      }
    } catch (e) {
      wx.showToast({ title: '网络错误', icon: 'none' });
    }
    this.setData({ submitting: false });
  },

  getDueStatus(dueDate) {
    if (!dueDate) return '';
    const now = new Date();
    const due = new Date(dueDate.replace(' ', 'T'));
    return due < now ? 'status-overdue' : '';
  }
});
