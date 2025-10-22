package main

import (
	"bytes"
	"fmt"
	"io"
	"net/http"
	"os"
	"strconv"
	"sync"
	"time"
	"net/url"
)

func main() {

	if len(os.Args) != 5 {
		fmt.Println("connection length duration(ms) useProxy")
		os.Exit(1)
	}

	conn, err1 := strconv.Atoi(os.Args[1])
	len, err2 := strconv.Atoi(os.Args[2])
	dur, err3 := strconv.Atoi(os.Args[3])
	useProxy, err4 := strconv.Atoi(os.Args[4])

	if err1 != nil || err2 != nil || err3 != nil || err4 != nil {
		fmt.Println("connection length duration(ms)")
		os.Exit(1)
	}
	
	var wg sync.WaitGroup
	start := make(chan struct{})
	stop := make(chan struct{})
	duration := time.Duration(dur) * time.Millisecond

	countChan := make(chan int, conn)
	
	if (useProxy == 1){
		for i := 1; i <= conn; i++ {
			wg.Add(1)
			go StressOnConnectionProxy(len, &wg, start, stop, countChan)
		}
	} else {
		for i := 1; i <= conn; i++ {
			wg.Add(1)
			go StressOnConnection(len, &wg, start, stop, countChan)
		}
	}
	
	time.Sleep(10 * time.Millisecond)

	close(start)
	startTime := time.Now()
	timer := time.NewTimer(duration)
	<-timer.C
	close(stop)

	wg.Wait()
	endTime := time.Now()
	close(countChan)

	totalCount := 0
	for count := range countChan {
		totalCount += count
	}

	fmt.Println("[Throughput]", int64(totalCount) * 1000 * 1000 * 1000 / (endTime.UnixNano() - startTime.UnixNano()))
}

func StressOnConnection(length int, wg *sync.WaitGroup, start <-chan struct{}, stop <-chan struct{}, countChan chan<- int){
	defer wg.Done()
	data := fmt.Sprintf("%*s", length, "A")
	reader := bytes.NewReader([]byte(data))

	
	client := &http.Client{
		Transport: &http.Transport{
			MaxIdleConnsPerHost: 1,
		},
		Timeout: 10 * time.Second,
	}
	
	count := 0

	<-start

	for {
		select {
		case <-stop:
			countChan <- count
			return
		default:
			reader.Seek(0, io.SeekStart)
			req, err := http.NewRequest("GET", "http://127.0.0.1:8080/echo", reader)
			if err != nil {
				fmt.Println("Error creating request:", err)
				return
			}

			resp, err := client.Do(req)
			if err != nil {
				fmt.Println("Error sending request:", err)
				return
			}
			defer resp.Body.Close()

			count++;
		}
	}
}

func StressOnConnectionProxy(length int, wg *sync.WaitGroup, start <-chan struct{}, stop <-chan struct{}, countChan chan<- int){
	defer wg.Done()
	data := fmt.Sprintf("%*s", length, "A")
	reader := bytes.NewReader([]byte(data))
	proxyURL, _ := url.Parse("http://127.0.0.1:8888")

	client := &http.Client{
		Transport: &http.Transport{
			Proxy: http.ProxyURL(proxyURL),
			MaxIdleConnsPerHost: 1,
		},
		Timeout: 10 * time.Second,
	}
	
	count := 0

	<-start

	for {
		select {
		case <-stop:
			countChan <- count
			return
		default:
			reader.Seek(0, io.SeekStart)
			req, err := http.NewRequest("GET", "http://127.0.0.1:8080/echo", reader)
			if err != nil {
				fmt.Println("Error creating request:", err)
				return
			}

			resp, err := client.Do(req)
			if err != nil {
				fmt.Println("Error sending request:", err)
				return
			}
			defer resp.Body.Close()

			count++;
		}
	}
}
