# This file is part of nvimcom R package
# 
# It is distributed under the GNU General Public License.
# See the file ../LICENSE for details.
# 
# (c) 2011 Jakson Aquino: jalvesaq@gmail.com
# 
###############################################################

.onLoad <- function(libname, pkgname) {
    if(Sys.getenv("NVIMR_TMPDIR") == "")
        return(invisible(NULL))
    library.dynam("nvimcom", pkgname, libname, local = FALSE)

    if(is.null(getOption("nvimcom.verbose")))
        options(nvimcom.verbose = 0)

    if(is.null(getOption("nvimcom.opendf")))
        options(nvimcom.opendf = TRUE)

    if(is.null(getOption("nvimcom.openlist")))
        options(nvimcom.openlist = FALSE)

    if(is.null(getOption("nvimcom.allnames")))
        options(nvimcom.allnames = FALSE)

    if(is.null(getOption("nvimcom.texerrs")))
        options(nvimcom.texerrs = TRUE)

    if(is.null(getOption("nvimcom.labelerr")))
        options(nvimcom.labelwarn = TRUE)

    if(Sys.getenv("NVIMR_SVRNM") %in% c("", "MacVim", "NoClientServer", "NoServerName"))
        options(nvimcom.nvimpager = FALSE)
    if(is.null(getOption("nvimcom.nvimpager"))){
        options(nvimcom.nvimpager = TRUE)
    }
    if(getOption("nvimcom.nvimpager"))
        options(pager = nvim.hmsg)
}

.onAttach <- function(libname, pkgname) {
    if(Sys.getenv("NVIMR_TMPDIR") == "")
        return(invisible(NULL))
    if(version$os == "mingw32")
        termenv <- "MinGW"
    else
        termenv <- Sys.getenv("TERM")

    if(interactive() && termenv != "" && termenv != "dumb" && Sys.getenv("NVIMR_COMPLDIR") != ""){
        dir.create(Sys.getenv("NVIMR_COMPLDIR"), showWarnings = FALSE)
        .C("nvimcom_Start",
           as.integer(getOption("nvimcom.verbose")),
           as.integer(getOption("nvimcom.opendf")),
           as.integer(getOption("nvimcom.openlist")),
           as.integer(getOption("nvimcom.allnames")),
           as.integer(getOption("nvimcom.labelerr")),
           path.package("nvimcom"),
           as.character(utils::packageVersion("nvimcom")),
           PACKAGE="nvimcom")
    }
}

.onUnload <- function(libpath) {
    if(is.loaded("nvimcom_Stop", PACKAGE = "nvimcom")){
        .C("nvimcom_Stop", PACKAGE="nvimcom")
        if(Sys.getenv("NVIMR_TMPDIR") != ""){
            unlink(paste0(Sys.getenv("NVIMR_TMPDIR"), "/nvimcom_running_",
                          Sys.getenv("NVIMR_ID")))
            if(.Platform$OS.type == "windows")
                unlink(paste0(Sys.getenv("NVIMR_TMPDIR"), "/rconsole_hwnd_",
                              Sys.getenv("NVIMR_SECRET")))
        }
        Sys.sleep(0.2)
        library.dynam.unload("nvimcom", libpath)
    }
}


nvim.edit <- function(name, file, title)
{
    if(file != "")
        stop("Feature not implemented. Use nvim to edit files.")
    if(is.null(name))
        stop("Feature not implemented. Use nvim to create R objects from scratch.")

    finalA <- paste0(Sys.getenv("NVIMR_TMPDIR"), "/nvimcom_edit_", Sys.getenv("NVIMR_ID"), "_A")
    finalB <- paste0(Sys.getenv("NVIMR_TMPDIR"), "/nvimcom_edit_", Sys.getenv("NVIMR_ID"), "_B")
    unlink(finalB)
    writeLines(text = "Waiting...", con = finalA)

    initial = paste0(Sys.getenv("NVIMR_TMPDIR"), "/nvimcom_edit_", round(runif(1, min = 100, max = 999)))
    sink(initial)
    dput(name)
    sink()

    .C("nvimcom_msg_to_nvim",
       paste0("ShowRObject('", initial, "')"),
       PACKAGE="nvimcom")

    while(file.exists(finalA))
        Sys.sleep(1)
    x <- eval(parse(finalB))
    unlink(initial)
    unlink(finalB)
    return(invisible(x))
}
