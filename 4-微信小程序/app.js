/**
 * 智能书架小程序 — 全局入口
 *
 * 新交互模式: 小程序生成任务QR码 → 用户出示给书架摄像头 → 机械爪执行 → 轮询完成
 */
const api = require('./config/api').default;

App({
  onLaunch() {
    this.autoLogin();
  },

  autoLogin() {
    const token = wx.getStorageSync('token');
    const userInfo = wx.getStorageSync('userInfo');
    if (token && userInfo) {
      this.globalData.token = token;
      this.globalData.userInfo = userInfo;
      return;
    }
    this.wxLogin();
  },

  wxLogin() {
    return new Promise((resolve) => {
      wx.login({
        success: (res) => {
          if (!res.code) {
            console.error('wx.login 失败');
            resolve(false);
            return;
          }
          wx.request({
            url: api.baseAPI + '/api/users/login',
            method: 'POST',
            data: { code: res.code },
            enableHttpDNS: false,
            success: (resp) => {
              if (resp.statusCode === 200 && resp.data.code === 200) {
                const { token, user } = resp.data.data;
                this.globalData.token = token;
                this.globalData.userInfo = user;
                wx.setStorageSync('token', token);
                wx.setStorageSync('userInfo', user);
                resolve(true);
              } else {
                // Offline fallback: create anonymous user
                this._createAnonymousUser().then(resolve);
              }
            },
            fail: () => {
              this._createAnonymousUser().then(resolve);
            }
          });
        },
        fail: () => {
          this._createAnonymousUser().then(resolve);
        }
      });
    });
  },

  _createAnonymousUser() {
    // Generate a local anonymous user ID
    let anonId = wx.getStorageSync('anon_user_id');
    if (!anonId) {
      anonId = 'anon_' + Date.now().toString(36) + Math.random().toString(36).substr(2, 6);
      wx.setStorageSync('anon_user_id', anonId);
    }
    return new Promise((resolve) => {
      wx.request({
        url: api.baseAPI + '/api/users/login',
        method: 'POST',
        data: { code: anonId, nickname: '读者' + anonId.slice(-4) },
        enableHttpDNS: false,
        success: (resp) => {
          if (resp.statusCode === 200 && resp.data.code === 200) {
            const { token, user } = resp.data.data;
            this.globalData.token = token;
            this.globalData.userInfo = user;
            wx.setStorageSync('token', token);
            wx.setStorageSync('userInfo', user);
          }
          resolve(false);
        },
        fail: () => resolve(false)
      });
    });
  },

  getToken() {
    return this.globalData.token || wx.getStorageSync('token');
  },

  getUserInfo() {
    return this.globalData.userInfo || wx.getStorageSync('userInfo');
  },

  isAdmin() {
    const user = this.getUserInfo();
    return user && user.role === 1;
  },

  refreshUserInfo() {
    const token = this.getToken();
    if (!token) return;
    wx.request({
      url: api.baseAPI + '/api/users/me',
      method: 'GET',
      header: { 'Authorization': 'Bearer ' + token },
      enableHttpDNS: false,
      success: (resp) => {
        if (resp.statusCode === 200 && resp.data.code === 200) {
          this.globalData.userInfo = resp.data.data;
          wx.setStorageSync('userInfo', resp.data.data);
        }
      }
    });
  },

  globalData: {
    token: null,
    userInfo: null
  }
});
