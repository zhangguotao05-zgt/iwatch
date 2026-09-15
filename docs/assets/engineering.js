(function () {
  "use strict";

  var scriptUrl = document.currentScript.src;
  document.querySelectorAll("table").forEach(function (table) {
    if (table.parentElement.classList.contains("table-wrap")) return;
    var wrapper = document.createElement("div");
    wrapper.className = "table-wrap";
    wrapper.tabIndex = 0;
    wrapper.setAttribute("role", "region");
    wrapper.setAttribute("aria-label", "表格，可横向滚动");
    table.parentNode.insertBefore(wrapper, table);
    wrapper.appendChild(table);
  });

  var diagrams = document.querySelectorAll(".mermaid");
  if (!diagrams.length) return;

  function showError() {
    diagrams.forEach(function (diagram) {
      if (diagram.querySelector("svg") && !diagram.querySelector(".error-icon")) return;
      var notice = document.createElement("p");
      notice.className = "diagram-error";
      notice.textContent = "图表未能渲染，请检查本地文档资源是否完整。";
      diagram.insertAdjacentElement("beforebegin", notice);
    });
  }

  var library = document.createElement("script");
  library.src = new URL("vendor/mermaid-11.17.0.min.js", scriptUrl).href;
  library.onerror = showError;
  library.onload = function () {
    if (!window.mermaid) return showError();
    window.mermaid.initialize({
      startOnLoad: false,
      theme: "base",
      securityLevel: "strict",
      themeVariables: {
        background: "#f6f8fb",
        primaryColor: "#e7edf5",
        primaryTextColor: "#172033",
        primaryBorderColor: "#7d8797",
        lineColor: "#536276",
        secondaryColor: "#dfe8f3",
        tertiaryColor: "#eef2f7",
        noteBkgColor: "#fff4cc",
        noteTextColor: "#172033",
        fontFamily: "Segoe UI, Microsoft YaHei UI, sans-serif"
      }
    });
    window.mermaid.run({ nodes: diagrams }).catch(showError);
  };
  document.head.appendChild(library);
}());
