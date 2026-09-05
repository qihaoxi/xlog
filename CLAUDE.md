# 上下文管理规则
- rg: content search;fd: find replace; Always use file search tools instead of reading whole directories
- 对于长任务，先制定计划记录到文档，包括整体计划和每步实现内容。每完成一个子任务sub-task更新到文档。完成后总结
- 除非必要，不要将整个大型代码文件放入上下文
- 在运行复杂查询后，请自动删除无关的中间日志（Tool Results）
- 编译在polyglot-c/cmake-build-debug中,cmake编译可以配置option
- 单元测试在tests,运行可以在cmake-build-debug/下直接./tests/xxxxxxx
