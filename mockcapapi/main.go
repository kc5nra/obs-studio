package main

import "flag"
import "log"
import "net/http"
import "time"

import _ "embed"

//go:embed out.json
var content []byte

func main() {
  var delayMs int
  flag.IntVar(&delayMs, "delayms", 0, "Delay responses by this time (in milliseconds)")
  flag.Parse()

  http.HandleFunc("/", func(res http.ResponseWriter, req *http.Request) {
    time.Sleep(time.Millisecond * time.Duration(delayMs))
    res.Write(content)
  })
  
  log.Fatal(http.ListenAndServe("127.0.0.1:8787", nil))
}


