/**
 * 借还 Tab — 借书/还书入口
 *
 * 两个大卡片:
 *   📖 我要借书 → 搜索/浏览选书 → 确认 → 生成任务QR
 *   📥 我要还书 → 查看借阅列表 → 生成任务QR
 */
const { Book } = require('../../model/book');
const app = getApp();

Page({
  data: {
    user: null,
    borrowed: [],
    loading: false,
  },

  onLoad() {
    this.refresh();
  },

  onShow() {
    this.refresh();
  },

  refresh() {
    const user = app.getUserInfo();
    this.setData({ user });
    if (user) {
      this.loadBorrowed(user.user_id);
    }
  },

  async loadBorrowed(userId) {
    try {
      const res = await Book.getMyBorrowed(userId);
      if (res.code === 200) {
        this.setData({ borrowed: res.data || [] });
      }
    } catch (e) {
      console.error('加载借阅失败', e);
    }
  },

  /** 借书：跳转到搜索页选书 */
  onBorrowBook() {
    wx.navigateTo({ url: '/pages/search/search?mode=borrow' });
  },

  /** 还书：直接创建还书任务 → 出 QR */
  async onReturnBooks() {
    const user = this.data.user;
    if (!user) { wx.showToast({title:'请先登录',icon:'none'}); return; }
    wx.showLoading({title:'创建还书任务...'});
    try {
      // Return task: books identified by K230 #1 during physical sorting
      const r = await Book.createTask('return', user.user_id, {});
      wx.hideLoading();
      if (r.code === 200) {
        wx.navigateTo({url:'/pages/qr-show/qr-show?taskId='+r.data.taskId+'&action=return'});
      } else {
        wx.showToast({title:r.message||'创建失败',icon:'none'});
      }
    } catch(e) {
      wx.hideLoading();
      wx.showToast({title:'网络错误',icon:'none'});
    }
  },

  /** 从 borrow 子页返回后可直接创建借书任务 */
  onBorrowWithId(e) {
    const { instanceId, title, category } = e.currentTarget.dataset;
    if (!instanceId) return;
    wx.navigateTo({
      url: '/pages/borrow/borrow-confirm?instanceId=' + instanceId +
           '&title=' + encodeURIComponent(title || '') +
           '&category=' + encodeURIComponent(category || '')
    });
  },

  getDueStatus(dueDate) {
    if (!dueDate) return '';
    const now = new Date();
    const due = new Date(dueDate.replace(' ', 'T'));
    return due < now ? 'status-overdue' : '';
  },

  getDueText(dueDate) {
    if (!dueDate) return '';
    const now = new Date();
    const due = new Date(dueDate.replace(' ', 'T'));
    const days = Math.ceil((due - now) / 86400000);
    if (days < 0) return '已逾期 ' + Math.abs(days) + ' 天 (¥' + (Math.abs(days) * 0.5).toFixed(1) + ')';
    if (days === 0) return '今天到期';
    return '剩余 ' + days + ' 天';
  }
});
