package middleware

import (
	"log"

	"github.com/gin-gonic/gin"
)

// Auth 鉴权中间件 - 目前预留，全部放过
func Auth() gin.HandlerFunc {
	return func(c *gin.Context) {
		token := c.Query("token")
		mac := c.Query("mac")

		// TODO: 实现真正的鉴权逻辑
		// 目前只打印日志，全部放过
		if token != "" || mac != "" {
			log.Printf("[Auth] token=%s, mac=%s, path=%s - PASSED (auth not enforced)", token, mac, c.Request.URL.Path)
		}

		c.Next()
	}
}
