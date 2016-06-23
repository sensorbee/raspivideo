package plugin

import (
	"gopkg.in/sensorbee/raspivideo.v0"
	"gopkg.in/sensorbee/sensorbee.v0/bql"
)

func init() {
	bql.RegisterGlobalSourceCreator("raspivideo", bql.SourceCreatorFunc(raspivideo.CreateSource))
}
