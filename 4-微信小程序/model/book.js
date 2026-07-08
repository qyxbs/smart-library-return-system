/**
 * 智能书架 API 模型层
 * 封装所有后端 API 调用
 */
const http = require('../utils/request');

const Book = {
  // ==================== Health ====================
  getHealth() {
    return http.get('/api/health', false);
  },

  // ==================== Shelf ====================
  getSlots() {
    return http.get('/api/storage-slot/list', false);
  },

  getSlot(id) {
    return http.get('/api/storage-slot/' + id);
  },

  getShelfBooks() {
    return http.get('/api/book-instance/shelf', false);
  },

  getInstanceBySlot(slotId) {
    return http.get('/api/book-instance/by-slot/' + slotId, false);
  },

  // ==================== Books ====================
  searchBook(keyword) {
    return http.get('/api/book/search?keyword=' + encodeURIComponent(keyword));
  },

  addBook(data) {
    return http.post('/api/book/add', data);
  },

  // ==================== Instances ====================
  getInstance(id) {
    return http.get('/api/book-instance/' + id);
  },

  getInstancePage(page = 1, size = 20, status = null) {
    return http.post('/api/book-instance/page', { page, size, status });
  },

  // ==================== Borrow / Return ====================
  getMyBorrowed(userId) {
    return http.get('/api/book-instance/my-borrowed?userId=' + userId);
  },

  // ==================== Tasks (QR-based) ====================
  /**
   * 创建借书/还书任务
   * @param {string} action - "borrow" | "return"
   * @param {number} userId
   * @param {object} payload - {instanceId: num} | {instanceIds: [num,...]}
   */
  createTask(action, userId, payload) {
    return http.post('/api/task/create', { action, userId, payload });
  },

  /**
   * 查询任务状态 (轮询)
   * @param {string} taskId
   */
  getTaskStatus(taskId) {
    return http.get('/api/task/' + taskId + '/status', false);
  },

  /**
   * 获取任务列表 (管理员)
   */
  getTaskList(status) {
    let path = '/api/task/list';
    if (status) path += '?status=' + status;
    return http.get(path);
  },

  // ==================== Users ====================
  getUserList(page = 1, size = 20) {
    return http.post('/api/users/page', { page, size });
  },

  promoteUser(userId, pin) {
    return http.post('/api/users/promote', { userId, pin });
  },

  demoteUser(userId, pin) {
    return http.post('/api/users/demote', { userId, pin });
  },

  freezeUser(userId, frozen, pin) {
    return http.post('/api/users/freeze', { userId, frozen, pin });
  },

  // ==================== Logs ====================
  getLogs(page = 1, size = 20) {
    return http.post('/api/operation-log/page', { page, size });
  },

  // ==================== System ====================
  getTaskQueueStatus() {
    return http.get('/api/task-queue/status');
  },

  getDeviceStatus() {
    return http.get('/api/book/device/status', false);
  },
};

module.exports = { Book };
