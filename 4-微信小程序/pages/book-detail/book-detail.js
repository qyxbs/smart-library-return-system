const { Book } = require('../../model/book');
const app = getApp();

Page({
  data: {
    instance: null,      // API data
    bookInfo: null,       // fallback
    loading: true,
    user: null,
    _loaded: false,
  },

  onLoad(options) {
    const u = app.getUserInfo();
    this.setData({ user: u });
    if (options.instanceId) {
      this.setData({ _loaded: true });
      this.loadInstance(options.instanceId);
    } else if (options.bookId) {
      this.setData({
        bookInfo: {
          id: options.bookId,
          title: decodeURIComponent(options.title || ''),
          category: decodeURIComponent(options.category || ''),
        },
        loading: false,
      });
    } else {
      this.setData({ loading: false });
    }
  },

  onShow() {
    if (this.data._loaded && !this.data.instance && !this.data.loading) {
      // reload if navigated back
      const u = app.getUserInfo();
      this.setData({ user: u });
    }
  },

  async loadInstance(id) {
    this.setData({ loading: true, instance: null });
    try {
      const res = await Book.getInstance(id);
      if (res.code === 200 && res.data) {
        const d = res.data;
        const u = this.data.user;
        this.setData({
          instance: d,
          loading: false,
          // compute in JS, pass as data for reliable WXML binding
          showBorrow: d.status === 3,
          showReturn: !!(u && d.status === 1 && d.borrower_id == u.user_id),
          isOverdue: !!(d.due_date && d.due_date < new Date().toISOString().slice(0,19).replace('T',' ')),
        });
      } else {
        this.setData({ loading: false });
        wx.showToast({ title: '未找到该书', icon: 'none' });
      }
    } catch (e) {
      this.setData({ loading: false });
      wx.showToast({ title: '网络错误', icon: 'none' });
    }
  },

  statusText(s) { return {0:'待贴标',1:'已借出',2:'待上架',3:'在架'}[s]||'未知'; },
  statusClass(s) { return {0:'status-pending',1:'status-borrowed',2:'status-pending',3:'status-on-shelf'}[s]||''; },

  async onBorrow() {
    const i=this.data.instance, u=this.data.user;
    if (!i||!u) return wx.showToast({title:'请先登录',icon:'none'});
    wx.showLoading({title:'创建借书任务...'});
    try {
      const r=await Book.createTask('borrow',u.user_id,{instanceId:i.instance_id});
      wx.hideLoading();
      if (r.code===200) {
        wx.navigateTo({url:'/pages/qr-show/qr-show?taskId='+r.data.taskId+'&action=borrow&bookTitle='+encodeURIComponent(i.title||'')});
      } else {
        wx.showToast({title:r.message||'创建失败',icon:'none'});
      }
    } catch(e) {
      wx.hideLoading();
      wx.showToast({title:'网络错误',icon:'none'});
    }
  },

  onReturn() {
    const i=this.data.instance;
    if (i) wx.navigateTo({url:'/pages/return/return?instanceId='+i.instance_id});
  }
});
