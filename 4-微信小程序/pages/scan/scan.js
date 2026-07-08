import Toast from '@vant/weapp/toast/toast';
import { Book } from '../../model/book';

const app = getApp();

Page({
  data: {
    instanceId: '',
    instance: null,
    book: null,
    isAdmin: false,
    currentUser: null,
    action: '',           // 'borrow' | 'return' | 'retrieve' | 're-shelve' | 'view'
    loading: false
  },

  onLoad(options) {
    this.setData({
      isAdmin: app.isAdmin(),
      currentUser: app.getUserInfo()
    });
    if (options.instanceId) {
      this.setData({ instanceId: options.instanceId });
      this.loadInstance(options.instanceId);
    }
  },

  onShow() {
    this.setData({
      isAdmin: app.isAdmin(),
      currentUser: app.getUserInfo()
    });
  },

  async loadInstance(id) {
    this.setData({ loading: true });
    try {
      const res = await Book.getBookInstance(id);
      if (res.code === 200) {
        const inst = res.data;
        this.setData({ instance: inst });
        // Determine action based on status and role
        this.determineAction(inst);
      } else {
        Toast('未找到该书');
      }
    } catch (e) {
      Toast('加载失败');
      console.error(e);
    }
    this.setData({ loading: false });
  },

  determineAction(inst) {
    const isAdmin = this.data.isAdmin;
    const user = this.data.currentUser;
    const userId = user ? user.user_id : 0;

    // status: 0=待贴标, 1=已借出, 2=待上架, 3=在架
    if (inst.status === 3 && isAdmin) {
      this.setData({ action: 'retrieve' });  // 管理员可取书
    } else if (inst.status === 3 && !isAdmin) {
      this.setData({ action: 'borrow' });    // 用户可借书
    } else if (inst.status === 1 && inst.borrower_id === userId) {
      this.setData({ action: 'return' });    // 借阅者本人可还书
    } else if (inst.status === 2 && isAdmin) {
      this.setData({ action: 're-shelve' }); // 管理员可重新上架
    } else if (inst.status === 1) {
      this.setData({ action: 'view' });      // 已被别人借走
    } else {
      this.setData({ action: 'view' });
    }
  },

  // ---- Actions ----

  async doBorrow() {
    const user = this.data.currentUser;
    if (!user) {
      Toast('请先登录');
      return;
    }
    wx.showLoading({ title: '借书中...' });
    try {
      const res = await Book.borrowBook({
        instanceId: this.data.instanceId,
        userId: user.user_id
      });
      wx.hideLoading();
      if (res.code === 200) {
        Toast.success('机械爪已推出，请取走书本');
        this.loadInstance(this.data.instanceId);
      } else {
        Toast.fail(res.message || '借书失败');
      }
    } catch (e) {
      wx.hideLoading();
      Toast.fail('请求失败');
    }
  },

  async doReturn() {
    wx.showLoading({ title: '归还中...' });
    try {
      const res = await Book.returnBook({ instanceId: this.data.instanceId });
      wx.hideLoading();
      if (res.code === 200) {
        Toast.success('归还成功！请将书交给管理员上架');
        this.loadInstance(this.data.instanceId);
      } else {
        Toast.fail(res.message || '归还失败');
      }
    } catch (e) {
      wx.hideLoading();
      Toast.fail('请求失败');
    }
  },

  async doRetrieve() {
    wx.showLoading({ title: '取书中...' });
    try {
      const res = await Book.retrieveBook({ instanceId: this.data.instanceId });
      wx.hideLoading();
      if (res.code === 200) {
        Toast.success('机械爪已推出，请取走书本');
      } else {
        Toast.fail(res.message || '取书失败');
      }
    } catch (e) {
      wx.hideLoading();
      Toast.fail('请求失败');
    }
  },

  async doReShelve() {
    wx.showLoading({ title: '上架中...' });
    try {
      const res = await Book.reShelveBook({ instanceId: this.data.instanceId });
      wx.hideLoading();
      if (res.code === 200) {
        Toast.success('上架成功！槽位: ' + res.data.remark);
        this.loadInstance(this.data.instanceId);
      } else {
        Toast.fail(res.message || '上架失败');
      }
    } catch (e) {
      wx.hideLoading();
      Toast.fail('请求失败');
    }
  },

  // ---- Scanner ----

  onScanCode() {
    const that = this;
    wx.scanCode({
      onlyFromCamera: true,
      scanType: ['qrCode', 'barCode'],
      success(res) {
        const code = res.result;
        // Try to parse instanceId from QR content
        const match = code.match(/instance[=:](\d+)/i) || code.match(/^(\d{15,20})$/);
        const id = match ? match[1] : code;
        that.setData({ instanceId: id });
        that.loadInstance(id);
      },
      fail() {
        Toast('扫码取消');
      }
    });
  },

  onInputConfirm(e) {
    const id = e.detail.value.trim();
    if (id) {
      this.setData({ instanceId: id });
      this.loadInstance(id);
    }
  },

  // ---- Helpers ----

  statusText(status) {
    const map = { 0: '待贴标', 1: '已借出', 2: '待上架', 3: '在架' };
    return map[status] || '未知';
  },

  actionText(action) {
    const map = {
      borrow: '借书', return: '还书',
      retrieve: '取书(机械爪)', 're-shelve': '重新上架',
      view: '查看'
    };
    return map[action] || '查看';
  }
});
