import { Book } from '../../model/book';

const app = getApp();

Page({
  data: {
    userInfo: null,
    isAdmin: false,
    borrowedBooks: []
  },

  onLoad() {
    this.loadUserInfo();
  },

  onShow() {
    this.loadUserInfo();
  },

  loadUserInfo() {
    const user = app.getUserInfo();
    this.setData({
      userInfo: user,
      isAdmin: user && user.role === 1
    });
    if (user) {
      this.loadBorrowed(user.user_id);
    }
  },

  async loadBorrowed(userId) {
    try {
      const res = await Book.getMyBorrowed(userId);
      if (res.code === 200) {
        this.setData({ borrowedBooks: res.data || [] });
      }
    } catch (e) {
      console.error('加载借阅失败', e);
    }
  },

  onBookTap(e) {
    const id = e.currentTarget.dataset.id;
    wx.navigateTo({ url: '/pages/scan/scan?instanceId=' + id });
  },

  onLogout() {
    wx.showModal({
      title: '提示',
      content: '确定要退出登录吗？',
      success: (res) => {
        if (res.confirm) {
          wx.removeStorageSync('token');
          wx.removeStorageSync('userInfo');
          app.globalData.token = null;
          app.globalData.userInfo = null;
          this.setData({ userInfo: null, isAdmin: false, borrowedBooks: [] });
          app.wxLogin();
        }
      }
    });
  },

  onLogin() {
    app.wxLogin();
    setTimeout(() => this.loadUserInfo(), 1500);
  }
});
