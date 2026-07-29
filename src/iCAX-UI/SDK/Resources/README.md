# Resources

`ResourceClient` 是按完整 Resource URL 路由的直接资源访问客户端。

它使用 Fetch 风格的 `HEAD/GET/POST/PUT/DELETE/OPTIONS`，但通过宿主
`requestResource` API 直接访问后端 `ResourceLibrary`，不发送
SDO 或 SDO 邮件。URL 已包含 Application/Product/Project/Scene
作用域，因此 bridge 请求不再另外传 ProjectID 或 SceneID。
`AppProxy`、`ProductProxy`、`ProjectProxy` 和 `SceneProxy` 都公开同一个
`resources`/`fetchResource()` 能力；代理层级不会限制目标 URL 的作用域。

```js
import {
  allocateResourceURL,
  makeResourceCollectionURL,
} from "../index.mjs";

const scope = {
  applicationId,
  productId,
  projectId,
  sceneId,
};
const { resourceId, url: resourceUrl } = allocateResourceURL(scope);

await sceneProxy.resources.create(resourceUrl, flatBufferBytes, {
  headers: {
    "Content-Type": "application/vnd.icax.flatbuffer",
  },
});

const response = await sceneProxy.resources.get(resourceUrl, {
  headers: {
    Accept: "application/vnd.icax.flatbuffer",
  },
});

const schemaVersion = response.headers.get("ICAX-Schema-Version");
const bytes = await response.arrayBuffer();
```

`create(url, body)` 等价于 `PUT + If-None-Match: *`，适合调用方提前
分配 GUID。若希望后端分配 GUID，可对 collection URL 调用
`post(collectionUrl, body)`，并从响应的 `Location` 读取新 URL。

资源变更通知仍可以使用事件系统；事件只携带 URL、ETag 和资源版本，
资源内容由本客户端按需读取。
