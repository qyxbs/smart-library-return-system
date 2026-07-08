/**
 * 我的 Tab — 个人信息 + 借阅列表 + 管理入口
 */
const { Book } = require('../../model/book');
const app = getApp();

Page({
  data: {
    user: null,
    isAdmin: false,
    borrowed: [],
    showPromote: false,
    pinInput: '',
    promoting: false,
  },

  onLoad() {
    this.refresh();
  },

  onShow() {
    this.refresh();
  },

  refresh() {
    const user = app.getUserInfo();
    this.setData({ user, isAdmin: app.isAdmin() });
    if (user) {
      this.loadBorrowed(user.user_id);
      app.refreshUserInfo();  // refresh to get latest fine_balance
      setTimeout(() => {
        const u = app.getUserInfo();
        if (u) this.setData({ user: u });
      }, 500);
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

  // ==================== PIN 提权 ====================

  onGoAdmin() {
    // Admin enters management panel
    wx.navigateTo({ url: '/pages/admin/admin' });
  },

  // Hidden: tap avatar 5 times to reveal PIN promotion
  _tapCount: 0,
  _tapTimer: null,
  onAvatarTap() {
    this._tapCount++;
    if (this._tapTimer) clearTimeout(this._tapTimer);
    if (this._tapCount >= 5) {
      this._tapCount = 0;
      this.setData({ showPromote: true, pinInput: '' });
    } else {
      this._tapTimer = setTimeout(() => { this._tapCount = 0; }, 1500);
    }
  },

  onPinInput(e) {
    // Vant van-field: e.detail is the string value
    const val = typeof e.detail === 'string' ? e.detail : (e.detail.value || '');
    this.setData({ pinInput: val });
  },

  async onPromoteConfirm() {
    const pin = (this.data.pinInput || '').trim();
    if (!pin) {
      wx.showToast({ title: '请输入 PIN 码', icon: 'none' });
      return;
    }

    const user = this.data.user;
    if (!user) return;

    this.setData({ promoting: true });
    try {
      const res = await Book.promoteUser(user.user_id, pin);
      if (res.code === 200) {
        wx.showToast({ title: '已提升为管理员', icon: 'success' });
        app.refreshUserInfo();
        setTimeout(() => this.refresh(), 500);
        this.setData({ showPromote: false });
      } else {
        wx.showToast({ title: res.message || 'PIN 码错误', icon: 'none' });
      }
    } catch (e) {
      wx.showToast({ title: '网络错误', icon: 'none' });
    }
    this.setData({ promoting: false });
  },

  onPromoteCancel() {
    this.setData({ showPromote: false });
  },

  // ==================== Actions ====================

  onLogin() {
    app.wxLogin().then(() => {
      setTimeout(() => this.refresh(), 500);
    });
  },

  onLogout() {
    wx.showModal({
      title: '退出登录',
      content: '确定要退出吗？',
      success: (res) => {
        if (res.confirm) {
          wx.removeStorageSync('token');
          wx.removeStorageSync('userInfo');
          app.globalData.token = null;
          app.globalData.userInfo = null;
          this.setData({ user: null, isAdmin: false, borrowed: [] });
          app.wxLogin();
        }
      }
    });
  },

  onBookTap(e) {
    const idx = e.currentTarget.dataset.index;
    const book = this.data.borrowed[idx];
    if (book) {
      wx.navigateTo({ url: '/pages/book-detail/book-detail?instanceId=' + book.instance_id });
    }
  },

  getDueStatus(dueDate) {
    if (!dueDate) return '';
    const due = new Date(dueDate.replace(' ', 'T'));
    return due < new Date() ? 'status-overdue' : '';
  },

  getDueText(dueDate) {
    if (!dueDate) return '';
    const now = new Date();
    const due = new Date(dueDate.replace(' ', 'T'));
    const days = Math.ceil((due - now) / 86400000);
    if (days < 0) return '已逾期 ' + Math.abs(days) + ' 天';
    if (days === 0) return '今天到期';
    return '剩余 ' + days + ' 天';
  }
});
