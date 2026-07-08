/**
 * 小程序网络请求封装
 * 自动带 token、401 重试、错误处理
 */
const app = getApp();

const BASE = 'https://YOUR_SERVER_DOMAIN';  // 替换为你的后端服务器域名

/**
 * 发起 HTTP 请求
 * @param {string} method - GET/POST/PUT/DELETE
 * @param {string} path - API 路径 (如 /api/health)
 * @param {object} data - 请求体 (POST/PUT)
 * @param {boolean} auth - 是否需要 token (默认 true)
 * @returns {Promise<object>} - {code, data, message}
 */
function request(method, path, data = null, auth = true) {
  return new Promise((resolve, reject) => {
    const header = { 'Content-Type': 'application/json' };
    if (auth) {
      const token = app ? app.getToken() : wx.getStorageSync('token');
      if (token) {
        header['Authorization'] = 'Bearer ' + token;
      }
    }

    wx.request({
      url: BASE + path,
      method: method,
      data: data,
      header: header,
      timeout: 30000,
      enableHttpDNS: false,  // 本地网络直连，禁用 HTTPDNS
      success(res) {
        if (res.statusCode === 200) {
          resolve(res.data);
        } else if (res.statusCode === 401) {
          // Token expired, try re-login once
          if (app && auth) {
            app.wxLogin().then(() => {
              // Retry with new token
              const newToken = app.getToken();
              header['Authorization'] = 'Bearer ' + newToken;
              wx.request({
                url: BASE + path,
                method: method,
                data: data,
                header: header,
                enableHttpDNS: false,
                success(r2) { resolve(r2.data); },
                fail: reject
              });
            });
          } else {
            reject(res);
          }
        } else {
          // Return error body so caller can handle
          resolve(res.data || { code: res.statusCode, message: '请求失败' });
        }
      },
      fail(err) {
        console.error(`[HTTP] ${method} ${path} failed:`, err);
        reject(err);
      }
    });
  });
}

const http = {
  get(path, auth = true)    { return request('GET', path, null, auth); },
  post(path, data, auth = true)  { return request('POST', path, data, auth); },
  put(path, data, auth = true)   { return request('PUT', path, data, auth); },
  del(path, auth = true)    { return request('DELETE', path, null, auth); },
};

module.exports = http;
