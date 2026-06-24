/* ================================================================
   AgentChat HTML 版 - 群任务帖子 API
   对齐 Vue 版 conversationPostApi.ts（CONVERSATION_POST_* 命令）
   ================================================================ */

import AppHttp from './httpClient.js';

const PostApi = {
  /**
   * 创建帖子
   * 对齐 Vue 版 createConversationPost → CONVERSATION_POST_CREATE
   */
  async create(data) {
    return AppHttp.post('/posts', {
      id: data.id,
      conversationId: data.conversationId,
      title: data.title,
      summary: data.summary || '',
      taskKind: data.taskKind || 'notice',
      actionType: data.actionType || 'read',
      resourceType: data.resourceType || 'none',
      resourceUrl: data.resourceUrl || '',
      deadlineAt: data.deadlineAt ? String(data.deadlineAt) : '',
      createdById: data.createdById || '',
      createdByName: data.createdByName || '',
    });
  },

  /**
   * 获取会话帖子列表
   * 对齐 Vue 版 listConversationPosts → CONVERSATION_POST_LIST
   */
  async list(conversationId) {
    return AppHttp.get(`/posts?conversation_id=${encodeURIComponent(conversationId)}`);
  },

  /**
   * 更新帖子
   * 对齐 Vue 版 updateConversationPost → CONVERSATION_POST_UPDATE
   */
  async update(postId, conversationId, data) {
    return AppHttp.put(`/posts/${postId}`, {
      postId,
      conversationId,
      title: data.title || '',
      summary: data.summary || '',
    });
  },

  /**
   * 关闭帖子
   * 对齐 Vue 版 closeConversationPost → CONVERSATION_POST_CLOSE
   */
  async close(postId, conversationId) {
    return AppHttp.put(`/posts/${postId}/close`, {
      postId,
      conversationId,
    });
  },

  /**
   * 响应帖子
   * 对齐 Vue 版 respondToConversationPost → CONVERSATION_POST_RESPONSE
   */
  async respond(data) {
    return AppHttp.post('/posts/respond', {
      conversationId: data.conversationId,
      postId: data.postId,
      actorUserId: data.actorUserId || '',
      actorName: data.actorName || '',
      actionType: data.actionType || 'read',
      responseType: data.responseType || 'read',
      confirmation: data.confirmation || '',
      content: data.content || '',
      attachmentsJson: data.attachmentsJson || '{}',
    });
  },
};

export default PostApi;
