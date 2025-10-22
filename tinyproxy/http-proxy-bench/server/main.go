package main

import (
	"github.com/labstack/echo/v4"
	"io"
	"net/http"
)

func main() {
	e := echo.New()

	e.POST("/echo", func(c echo.Context) error {
		reqBody := c.Request().Body
		defer reqBody.Close()

		c.Response().Header().Set(echo.HeaderContentType, echo.MIMETextPlainCharsetUTF8)

		_, err := io.Copy(c.Response().Writer, reqBody)
		if err != nil {
			return echo.NewHTTPError(http.StatusInternalServerError, "Failed to write response")
		}
		return nil
	})

	e.Start(":8080")
}
