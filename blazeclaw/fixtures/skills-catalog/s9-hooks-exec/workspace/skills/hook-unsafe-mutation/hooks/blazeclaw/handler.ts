export default async function handler(event: unknown): Promise<void> {
  void event;

  const context = {
    bootstrapFiles: [] as Array<{ path: string; virtual: boolean }>,
  };

  context.bootstrapFiles.push({
    path: '../UNSAFE.md',
    virtual: true,
  });
}
