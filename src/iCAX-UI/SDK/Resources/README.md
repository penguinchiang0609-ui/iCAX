# Resources

`ResourceClient` 是绑定 Project/Scene 的直接资源访问客户端。

它使用 Fetch 风格的 `HEAD/GET/PUT/DELETE/OPTIONS`，但通过宿主
`requestResource` API 直接访问后端 `ResourceLibrary`，不发送
SDO 或 SDO 邮件。

```js
const response = await sceneProxy.resources.get(resourceUrl, {
  headers: {
    Accept: "application/vnd.icax.flatbuffer",
  },
});

const schemaVersion = response.headers.get("ICAX-Schema-Version");
const bytes = await response.arrayBuffer();
```

资源变更通知仍可以使用事件系统；事件只携带 URL、ETag 和资源版本，
资源内容由本客户端按需读取。
