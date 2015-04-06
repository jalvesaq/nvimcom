\name{nvimcom-package}
\alias{nvimcom-package}
\alias{nvimcom}
\docType{package}
\title{Allow the communication between Nvim and R }
\description{This package provides a TCP/IP server to allow the communication
  between R and Nvim-R. It also has some functions called by the
  plugin. By default, the elements of data.frames are shown in the object
  browser, but not the elements of other types of lists, and, by default,
  names of objects which begin with a dot are omitted. The following options
  control the nvimcom behavior:

  The \samp{nvimcom.verbose} option controls the amount of debugging
  information printed on R Console. Its default value is 0. If the value is 1,
  the package version will be output on startup. If the value is 2, the time
  required to update the Object Browser will be printed. This is useful if you
  suspect that R is noticeably slower when the Object Browser is open in the
  Nvim-R plugin. Higher values, up to 4, will make the package print
  information verbosely which is useful only if you want to either fix a bug
  or understand how nvimcom works.

  Below is an example of how to load \pkg{nvimcom}:

  \preformatted{
    if(interactive()){
        if(Sys.getenv("NVIMR_TMPDIR") != ""){
            options(nvimcom.verbose = 1)
            library(nvimcom)
        }
    }

  }
}
\seealso{\link[base]{Startup}}
\author{
Jakson Alves de Aquino \email{jalvesaq@gmail.com}
}
\keyword{ package }
