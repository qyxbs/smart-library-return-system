import Toast from '@vant/weapp/toast/toast';
import { Book } from '../../model/book';

Page({
  data: {
    title: '',
    author: '',
    categoryIndex: 0,
    categories: ['文学休闲区', '少儿教辅区', '人文社科区', '科技艺术区']
  },

  onInputChange(e) {
    const { key } = e.currentTarget.dataset;
    this.setData({ [key]: e.detail });
  },

  onCategoryChange(e) {
    this.setData({ categoryIndex: e.detail.value });
  },

  onCategoryTap(e) {
    this.setData({ categoryIndex: e.currentTarget.dataset.index });
  },

  async onSubmit() {
    const { title, categoryIndex, categories } = this.data;
    if (!title.trim()) {
      return Toast.fail('请输入书名');
    }

    Toast.loading({ message: '正在录入...', forbidClick: true });
    try {
      const res = await Book.addBook({
        title: title.trim(),
        author: this.data.author.trim() || '管理员',
        category: categories[categoryIndex]
      });
      if (res.code === 200) {
        const msg = res.warning ? ('录入成功，' + res.warning) : '录入成功，标签已打印';
        Toast.success(msg);
        setTimeout(() => wx.navigateBack(), 2000);
      } else {
        Toast.fail(res.message || '录入失败');
      }
    } catch (e) {
      console.error('录入失败', e);
      Toast.fail('请求失败，请检查网络');
    }
  }
});
