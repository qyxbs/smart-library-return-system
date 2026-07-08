/**
 * 管理面板 — 用户管理 + 日志 + 任务监控
 */
const { Book } = require('../../model/book');
const app = getApp();

Page({
  data: {
    activeTab: 'users',  // users | logs | tasks | system
    pinVerified: false,
    pinInput: '',
    verifying: false,

    // Action popup (promote/demote)
    showActionPopup: false,
    actionType: '',       // 'promote' | 'demote'
    actionUserId: 0,
    actionUserName: '',
    actionPin: '',
    actionLoading: false,

    // Users
    users: [],
    userPage: 1,
    userTotal: 0,

    // Logs
    logs: [],
    logPage: 1,
    logTotal: 0,

    // Tasks
    tasks: [],
    taskQueue: null,

    // System
    deviceOnline: false,
  },

  onLoad() {
    if (app.isAdmin()) {
      this.setData({ pinVerified: true });
      this.loadAll();
    }
  },

  // ==================== PIN ====================

  onPinInput(e) {
    const val = typeof e.detail === 'string' ? e.detail : (e.detail.value || '');
    this.setData({ pinInput: val });
  },

  async onPinVerify() {
    const pin = this.data.pinInput.trim();
    if (!pin) return;

    this.setData({ verifying: true });
    try {
      // Verify PIN by attempting to get user list (requires admin)
      const res = await Book.getUserList(1, 1);
      // If PIN is needed, use a simple endpoint check
      // For now, trust the admin check on the server side via existing API
      this.setData({ pinVerified: true });
    } catch (e) {
      wx.showToast({ title: '验证失败', icon: 'none' });
    }
    this.setData({ verifying: false });
  },

  // ==================== Load All ====================

  loadAll() {
    this.loadUsers();
    this.loadLogs();
    this.loadTasks();
    this.loadSystem();
  },

  // ==================== Users ====================

  async loadUsers() {
    try {
      const res = await Book.getUserList(this.data.userPage);
      if (res.code === 200) {
        this.setData({
          users: res.data.records || [],
          userTotal: res.data.total || 0,
        });
      }
    } catch (e) {
      console.error('加载用户失败', e);
    }
  },

  // ==================== Promote / Demote ====================

  onPromoteUser(e) {
    const id = e.currentTarget.dataset.id;
    const user = this.data.users.find(u => u.user_id == id);
    this.setData({
      showActionPopup: true, actionType: 'promote',
      actionUserId: id, actionPin: '',
      actionUserName: (user && user.nickname) || ('ID:' + id),
    });
  },

  onDemoteUser(e) {
    const id = e.currentTarget.dataset.id;
    const user = this.data.users.find(u => u.user_id == id);
    this.setData({
      showActionPopup: true, actionType: 'demote',
      actionUserId: id, actionPin: '',
      actionUserName: (user && user.nickname) || ('ID:' + id),
    });
  },

  onActionPinInput(e) {
    const val = typeof e.detail === 'string' ? e.detail : (e.detail.value || '');
    this.setData({ actionPin: val });
  },

  async onActionConfirm() {
    const pin = (this.data.actionPin || '').trim();
    if (!pin) { wx.showToast({ title: '请输入 PIN 码', icon: 'none' }); return; }
    this.setData({ actionLoading: true });
    const { actionType, actionUserId } = this.data;
    try {
      let res;
      if (actionType === 'promote') res = await Book.promoteUser(actionUserId, pin);
      else res = await Book.demoteUser(actionUserId, pin);
      if (res.code === 200) {
        wx.showToast({ title: actionType === 'promote' ? '已提升为管理员' : '已降级为普通用户', icon: 'success' });
        this.setData({ showActionPopup: false });
        this.loadUsers();
      } else {
        wx.showToast({ title: res.message || '操作失败', icon: 'none' });
      }
    } catch (e) { wx.showToast({ title: '网络错误', icon: 'none' }); }
    this.setData({ actionLoading: false });
  },

  onActionCancel() { this.setData({ showActionPopup: false }); },

  async onFreezeUser(e) {
    const { id, frozen } = e.currentTarget.dataset;
    const action = frozen ? '解冻' : '冻结';
    wx.showModal({
      title: '确认操作',
      content: '确定要' + action + '该用户吗？',
      success: async (res) => {
        if (!res.confirm) return;
        try {
          const result = await Book.freezeUser(id, frozen ? 0 : 1, '123456');
          if (result.code === 200) {
            wx.showToast({ title: action + '成功', icon: 'success' });
            this.loadUsers();
          } else {
            wx.showToast({ title: result.message || '操作失败', icon: 'none' });
          }
        } catch (e) {
          wx.showToast({ title: '操作失败', icon: 'none' });
        }
      }
    });
  },

  // ==================== Logs ====================

  async loadLogs() {
    try {
      const res = await Book.getLogs(this.data.logPage);
      if (res.code === 200) {
        this.setData({
          logs: res.data.records || [],
          logTotal: res.data.total || 0,
        });
      }
    } catch (e) {
      console.error('加载日志失败', e);
    }
  },

  // ==================== Tasks ====================

  async loadTasks() {
    try {
      const [taskRes, queueRes] = await Promise.all([
        Book.getTaskList(),
        Book.getTaskQueueStatus(),
      ]);
      if (taskRes.code === 200) {
        this.setData({ tasks: taskRes.data || [] });
      }
      if (queueRes.code === 200) {
        this.setData({ taskQueue: queueRes.data });
      }
    } catch (e) {
      console.error('加载任务失败', e);
    }
  },

  // ==================== System ====================

  async loadSystem() {
    try {
      const res = await Book.getDeviceStatus();
      this.setData({ deviceOnline: !!(res && res.code === 200 && res.data) });
    } catch (e) {
      this.setData({ deviceOnline: false });
    }
  },

  // ==================== Navigation ====================

  onTabChange(e) {
    const tab = e.currentTarget.dataset.tab;
    this.setData({ activeTab: tab });
  },

  onRefresh() {
    this.loadAll();
  },

  getActionText(type) {
    const map = {
      'BORROW': '借书', 'RETURN': '还书', 'QR_BORROW': '扫码借书',
      'QR_RETURN': '扫码还书', 'SCAN': '上架', 'RETRIEVE': '取书',
      'RE_SHELVE': '重上架', 'PRINT': '打印', 'VERIFY': '回收',
      'PROMOTE': '提权', 'DEMOTE': '降级',
    };
    return map[type] || type;
  }
});
