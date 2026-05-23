package main

import (
	"fmt"
	"os"

	"github.com/jessevdk/go-flags"
	modelbaker "github.com/karetskiiVO/graphics-course/tasks/model_bakery/baker/model-baker"
)

func main() {
	var opts struct {
		Args struct {
			File string
		} `positional-args:"yes" required:"1"`
	}

	parser := flags.NewParser(&opts, flags.HelpFlag|flags.PassDoubleDash)
	if _, err := parser.Parse(); err != nil {
		fmt.Fprintln(os.Stdout, err)
		os.Exit(1)
	}

	baker := modelbaker.NewBaker()
	if err := baker.BakeScene(opts.Args.File); err != nil {
		panic(err)
	}
}
