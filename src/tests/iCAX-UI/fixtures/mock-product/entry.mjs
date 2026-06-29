export function mountProduct(context) {
  context?.events?.push?.(`product:${context.mode}`);
}

export function mountProject(context) {
  context?.events?.push?.(`project:${context.mode}`);
}
