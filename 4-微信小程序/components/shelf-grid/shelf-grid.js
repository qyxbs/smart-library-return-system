/**
 * shelf-grid 组件 — 16 格书架可视化
 *
 * 数据格式:
 *   slot = {slot_id, coord_x, coord_y, is_occupied, remark,
 *           bookTitle, bookAuthor, bookId, instanceId, category}
 */
Component({
  properties: {
    slots: {
      type: Array,
      value: [],
      observer: '_onSlotsChange'
    }
  },

  data: {
    layer1Slots: [],  // coord_x = 1 (一层)
    layer2Slots: [],  // coord_x = 2 (二层)
  },

  methods: {
    _onSlotsChange(slots) {
      if (!slots || !slots.length) return;
      this.setData({
        layer1Slots: slots.filter(s => s.coord_x === 1),
        layer2Slots: slots.filter(s => s.coord_x === 2),
      });
    },

    onSlotTap(e) {
      const slotId = e.currentTarget.dataset.slotId;
      this.triggerEvent('tap', { slotId: parseInt(slotId) || slotId });
    },

    getCategoryClass(category) {
      if (!category) return 'cat-other';
      if (category.includes('教辅') || category.includes('教育')) return 'cat-tutor';
      if (category.includes('文学') || category.includes('小说')) return 'cat-lit';
      if (category.includes('科技') || category.includes('艺术')) return 'cat-tech';
      return 'cat-other';
    },

    getStatusClass(isOccupied) {
      return isOccupied ? 'occupied' : 'empty';
    }
  }
});
