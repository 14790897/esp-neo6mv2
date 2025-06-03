// GPS 智能码表 - 前端交互脚本

class GPSTracker {
    constructor() {
        this.statusIndicator = document.getElementById('statusIndicator');
        this.mainContent = document.getElementById('main');
        this.downloadList = document.getElementById('downloadList');
        this.isOnline = true;
        this.lastUpdateTime = Date.now();
        
        this.init();
    }
    
    init() {
        // 页面加载完成后立即获取数据
        this.fetchData();
        this.fetchDownloadList();
        
        // 设置定时刷新
        setInterval(() => this.fetchData(), 2000);
        setInterval(() => this.fetchDownloadList(), 10000); // 下载列表更新频率较低
        
        // 监听网络状态
        window.addEventListener('online', () => this.setOnlineStatus(true));
        window.addEventListener('offline', () => this.setOnlineStatus(false));
        
        // 添加按钮点击反馈
        this.addButtonFeedback();
    }
    
    async fetchData() {
        try {
            const response = await fetch('/data');
            if (!response.ok) throw new Error('Network response was not ok');
            
            const html = await response.text();
            this.mainContent.innerHTML = html;
            this.setOnlineStatus(true);
            this.lastUpdateTime = Date.now();
            
        } catch (error) {
            console.error('Failed to fetch GPS data:', error);
            this.setOnlineStatus(false);
            this.showErrorMessage('无法获取GPS数据，请检查网络连接');
        }
    }
    
    async fetchDownloadList() {
        try {
            const response = await fetch('/downloads');
            if (!response.ok) throw new Error('Network response was not ok');
            
            const html = await response.text();
            // 提取下载列表部分
            const parser = new DOMParser();
            const doc = parser.parseFromString(html, 'text/html');
            const downloadSection = doc.querySelector('.download-list');
            
            if (downloadSection) {
                this.downloadList.innerHTML = downloadSection.innerHTML;
            }
            
        } catch (error) {
            console.error('Failed to fetch download list:', error);
            this.downloadList.innerHTML = '<div class="no-data">📭 无法加载下载列表</div>';
        }
    }
    
    setOnlineStatus(online) {
        this.isOnline = online;
        if (this.statusIndicator) {
            this.statusIndicator.className = `status-indicator ${online ? 'status-online' : 'status-offline'}`;
        }
    }
    
    showErrorMessage(message) {
        this.mainContent.innerHTML = `
            <div class="card">
                <div class="card-title">⚠️ 连接错误</div>
                <div class="loading-container">
                    <div style="font-size: 3em; margin-bottom: 20px;">❌</div>
                    <div class="loading-text">${message}</div>
                    <div style="margin-top: 20px;">
                        <button onclick="location.reload()" class="btn">🔄 重新加载</button>
                    </div>
                </div>
            </div>
        `;
    }
    
    addButtonFeedback() {
        // 为所有按钮添加点击反馈
        document.addEventListener('click', (e) => {
            if (e.target.classList.contains('btn')) {
                e.target.style.transform = 'scale(0.95)';
                setTimeout(() => {
                    e.target.style.transform = '';
                }, 150);
            }
        });
        
        // 为表单提交添加加载状态
        document.addEventListener('submit', (e) => {
            const button = e.target.querySelector('button[type="submit"]');
            if (button) {
                const originalText = button.innerHTML;
                button.innerHTML = '⏳ 处理中...';
                button.disabled = true;
                
                // 3秒后恢复按钮状态（防止卡住）
                setTimeout(() => {
                    button.innerHTML = originalText;
                    button.disabled = false;
                }, 3000);
            }
        });
    }
    
    // 格式化时间戳
    formatTimestamp(timestamp) {
        const date = new Date(timestamp);
        return date.toLocaleString('zh-CN', {
            year: 'numeric',
            month: '2-digit',
            day: '2-digit',
            hour: '2-digit',
            minute: '2-digit',
            second: '2-digit'
        });
    }
    
    // 显示通知
    showNotification(message, type = 'info') {
        const notification = document.createElement('div');
        notification.className = `notification notification-${type}`;
        notification.innerHTML = `
            <div class="notification-content">
                <span class="notification-icon">${type === 'success' ? '✅' : type === 'error' ? '❌' : 'ℹ️'}</span>
                <span class="notification-message">${message}</span>
            </div>
        `;
        
        // 添加通知样式
        notification.style.cssText = `
            position: fixed;
            top: 20px;
            right: 20px;
            background: ${type === 'success' ? '#27ae60' : type === 'error' ? '#e74c3c' : '#3498db'};
            color: white;
            padding: 15px 20px;
            border-radius: 10px;
            box-shadow: 0 4px 15px rgba(0,0,0,0.2);
            z-index: 1000;
            animation: slideIn 0.3s ease;
        `;
        
        document.body.appendChild(notification);
        
        // 3秒后自动移除
        setTimeout(() => {
            notification.style.animation = 'slideOut 0.3s ease';
            setTimeout(() => {
                if (notification.parentNode) {
                    notification.parentNode.removeChild(notification);
                }
            }, 300);
        }, 3000);
    }
}

// 添加通知动画样式
const style = document.createElement('style');
style.textContent = `
    @keyframes slideIn {
        from { transform: translateX(100%); opacity: 0; }
        to { transform: translateX(0); opacity: 1; }
    }
    
    @keyframes slideOut {
        from { transform: translateX(0); opacity: 1; }
        to { transform: translateX(100%); opacity: 0; }
    }
    
    .notification-content {
        display: flex;
        align-items: center;
        gap: 10px;
    }
    
    .notification-icon {
        font-size: 1.2em;
    }
    
    .notification-message {
        font-weight: 500;
    }
`;
document.head.appendChild(style);

// 页面加载完成后初始化
document.addEventListener('DOMContentLoaded', () => {
    window.gpsTracker = new GPSTracker();
});

// 全局错误处理
window.addEventListener('error', (e) => {
    console.error('Global error:', e.error);
    if (window.gpsTracker) {
        window.gpsTracker.showNotification('页面发生错误，请刷新重试', 'error');
    }
});

// 网络状态监控
let lastOnlineStatus = navigator.onLine;
setInterval(() => {
    const currentStatus = navigator.onLine;
    if (currentStatus !== lastOnlineStatus) {
        lastOnlineStatus = currentStatus;
        if (window.gpsTracker) {
            window.gpsTracker.setOnlineStatus(currentStatus);
            window.gpsTracker.showNotification(
                currentStatus ? '网络连接已恢复' : '网络连接已断开',
                currentStatus ? 'success' : 'error'
            );
        }
    }
}, 1000);
