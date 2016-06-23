package plugin

import (
	"github.com/nobu-k/raspivideo"
	"gopkg.in/sensorbee/sensorbee.v0/bql"
)

func init() {
	bql.RegisterGlobalSourceCreator("raspivideo", bql.SourceCreatorFunc(raspivideo.CreateSource))
}
