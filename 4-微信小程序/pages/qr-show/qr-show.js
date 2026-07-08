const { Book } = require('../../model/book');
const POLL_MS = 1500;
const MAX_POLL = 200;  // 5 min

Page({
  data: {
    taskId: '',
    action: '', title: '', count: 0,
    status: 'loading',
    qrUrl: '',
    statusText: '创建任务中...',
    timerText: '',
    _timer: null,
    _polls: 0,
  },

  onLoad(options) {
    const taskId = options.taskId || '';
    const action = options.action || 'borrow';
    const title = decodeURIComponent(options.bookTitle || '');
    const count = parseInt(options.bookCount) || 0;
    this.setData({ taskId, action, title, count });
    if (taskId) this.start();
  },

  onUnload() { this.stop(); },

  start() {
    this.poll();
    this.setData({ _timer: setInterval(() => this.poll(), POLL_MS) });
  },

  stop() {
    if (this.data._timer) { clearInterval(this.data._timer); this.setData({ _timer: null }); }
  },

  async poll() {
    if (this.data._polls >= MAX_POLL) { this.stop(); this.setData({ status:'expired', statusText:'任务已超时' }); return; }
    this.setData({ _polls: this.data._polls + 1 });

    try {
      const res = await Book.getTaskStatus(this.data.taskId);
      if (res.code !== 200) return;

      const t = res.data;
      const st = t.status;

      // Build QR URL on first poll
      if (!this.data.qrUrl && t.qrContent) {
        const qrContent = t.qrContent || JSON.stringify({t:this.data.taskId,a:this.data.action,ts:Math.floor(Date.now()/1000)});
        this.setData({ qrUrl: 'https://api.qrserver.com/v1/create-qr-code/?size=300x300&data=' + encodeURIComponent(qrContent) });
      }

      // Countdown: use 300s total, decrement each poll
      if (!this._startTime && t.createdAt) {
        this._startTime = Date.now();
        this._totalSec = 300; // 5 min
      }
      if (this._startTime) {
        const elapsed = Math.floor((Date.now() - this._startTime) / 1000);
        const s = Math.max(0, this._totalSec - elapsed);
        this.setData({ timerText: Math.floor(s/60)+':'+String(s%60).padStart(2,'0') });
      }

      if (st === 'pending') {
        this.setData({ status: 'waiting', statusText: '请将二维码对准书架摄像头' });
      } else if (st === 'processing') {
        this.setData({ status: 'processing', statusText: '机械爪执行中，请稍候...' });
      } else if (st === 'done') {
        this.stop();
        this.setData({ status: 'done', statusText: '完成！' });
        const r = t.result || {};
        const msg = this.data.action === 'borrow'
          ? '《'+this.data.title+'》借书成功！\n请在取书区取走书本'
          : (r.message || '还书完成');
        wx.showModal({ title:'操作成功', content:msg, showCancel:false, confirmText:'好的',
          success:() => wx.navigateBack({delta:2}) });
      } else if (st === 'failed') {
        this.stop();
        this.setData({ status:'failed', statusText:(t.result||{}).error||'操作失败' });
        wx.showModal({ title:'操作失败', content:this.data.statusText, showCancel:false, confirmText:'返回',
          success:() => wx.navigateBack() });
      } else if (st === 'expired') {
        this.stop();
        this.setData({ status:'expired', statusText:'任务已过期，请重新生成' });
        wx.showModal({ title:'已过期', content:'请返回重新生成二维码', showCancel:false, confirmText:'返回',
          success:() => wx.navigateBack() });
      }
    } catch (e) { /* retry next poll */ }
  },

  onRetry() { wx.navigateBack(); },
});
